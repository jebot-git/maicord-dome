#!/usr/bin/env python3
"""Real MaiCord Wayland window and tray inside a disposable DomeD session. No account."""
import json, os, re, signal, socket, subprocess, sys, tempfile, time, urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'dome'))
from launch import prepare_fonts, appearance, environment
from PIL import Image, ImageChops
if os.environ.get('DOMECORD_UI_BUS')!='1':
    os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'DOMECORD_UI_BUS':'1'})
signal.pthread_sigmask(signal.SIG_SETMASK,[])
out=ROOT/'dome/validation';out.mkdir(exist_ok=True)
children=[];logs=[]
def start(command,env,name,**kw):
    stream=(out/(name+'.log')).open('w');logs.append(stream)
    child=subprocess.Popen(command,env=env,stdout=stream,stderr=stream,**kw);children.append(child);return child

def wait(fn,timeout=40):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        try:
            value=fn()
            if value:return value
        except (OSError,ValueError,KeyError,subprocess.CalledProcessError):pass
        time.sleep(.1)
    raise AssertionError('Timed out; inspect '+str(out))
try:
    settings=appearance();settings.update(font_size=16,font_scale=1)
    if '--light' in sys.argv:settings['light']=True
    fonts,records=prepare_fonts(settings)
    (out/('ui-light.json' if settings.get('light') else 'ui.json')).unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix='dome-discord-ui-') as temp:
        rd,wr=os.pipe();start(['Xvfb','-displayfd',str(wr),'-screen','0','1440x1000x24','-nolisten','tcp'],os.environ.copy(),'xvfb',pass_fds=(wr,));os.close(wr)
        with os.fdopen(rd) as stream:screen=':'+stream.readline().strip()
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        env={**os.environ,'DISPLAY':screen,'XDG_RUNTIME_DIR':temp,'XDG_CONFIG_HOME':temp+'/config','XDG_DATA_HOME':temp+'/data','XDG_STATE_HOME':temp+'/state','XDG_CACHE_HOME':temp+'/cache','DOME_PREVIEW_PORT':str(port),'DOME_LOGIN_SESSION':'0','DOME_SESSION_RESTORE':'0','DOME_APPEARANCE_SERVICE':'0','DOME_RENDERER':'gles','LIBGL_ALWAYS_SOFTWARE':'1','RUST_LOG':'warn','DBUS_SYSTEM_BUS_ADDRESS':'unix:path='+temp+'/no-system-bus'}
        for service in ['XR','TRAY','KDECONNECT','POLKIT','KWALLET','IME']:env['DOME_START_'+service]='0'
        for key in ['WAYLAND_DISPLAY','WAYLAND_SOCKET','GALLIUM_DRIVER','MESA_LOADER_DRIVER_OVERRIDE']:env.pop(key,None)
        compositor=start([str(ROOT.parent/'dome-compositor/target/debug/dome-compositor'),'--backend','nested'],env,'compositor')
        http=urllib.request.build_opener(urllib.request.ProxyHandler({}));base=f'http://127.0.0.1:{port}'
        def state():
            with http.open(base+'/api/state',timeout=2) as response:return json.load(response)['scene']
        wait(state)
        runtime=Path(temp)
        desktop=wait(lambda:next((str(p).removesuffix('.dome-session.json') for p in runtime.glob('*.dome-session.json') if json.loads(p.read_text())['realm']=='desktop'),None))
        native={**env,**{k:v for k,v in environment(settings,fonts).items() if k.startswith('DOMECORD_')},'WAYLAND_DISPLAY':desktop,'WAYLAND_DEBUG':'client'}
        app=start([str(ROOT.parent/'maicord-dome-build/MaiCord')],native,'client')
        window=wait(lambda:next((w for w in state()['windows'] if w['app_id']=='org.domed.Discord'),None))
        wait(lambda:'first_present' in (runtime/'state/maicord/logs/maicord.log').read_text())
        time.sleep(.4)
        (out/'scene.json').write_text(json.dumps(state(),indent=2))
        import shutil
        for logfile in runtime.rglob('*.log'):
            if logfile.is_file():shutil.copy2(logfile,out/('runtime-'+logfile.name))
        assert app.poll() is None,'Client exited after mapping'
        # Capture the real X11 host output; the application's surface inside it is native Wayland.
        from Xlib import X,display
        xd=display.Display(screen)
        host=wait(lambda:next((w for w in xd.screen().root.query_tree().children if str(w.get_wm_name() or '').startswith('Dome')),None))
        g=host.get_geometry();raw=host.get_image(0,0,g.width,g.height,X.ZPixmap,0xffffffff)
        screenshot=Image.frombytes('RGB',(g.width,g.height),raw.data,'raw','BGRX')
        screenshot.save(out/('desktop-light.png' if settings.get('light') else 'desktop.png'));xd.close()
        if settings.get('light'):
            r=window['desktop']
            title=screenshot.crop((round(r['x']+200),round(r['y']+40),round(r['x']+530),round(r['y']+100)))
            # This area is inside the white card. A white bitmap rendered without
            # its foreground tint produces zero dark pixels, despite successful input.
            dark=sum(1 for red,green,blue in title.getdata() if max(red,green,blue)<100)
            assert dark>40,('Bitmap text lost its foreground color in light mode',dark)

        assert len(screenshot.getcolors(g.width*g.height) or [])>12,'Blank client image'
        items=subprocess.check_output(['gdbus','call','--session','--dest','org.kde.StatusNotifierWatcher','--object-path','/StatusNotifierWatcher','--method','org.freedesktop.DBus.Properties.Get','org.kde.StatusNotifierWatcher','RegisteredStatusNotifierItems'],text=True,timeout=5,stderr=subprocess.DEVNULL)
        if 'StatusNotifierItem' not in items:
            print('TRAY MISSING',items,flush=True)
        assert 'StatusNotifierItem' in items,items
        # Query this client's exported menu, independently of the host rendering.
        service=re.search(r"org\.kde\.StatusNotifierItem[^'\s,]*",items).group(0)
        service=service.split('/')[0]
        identity=subprocess.check_output(['gdbus','call','--session','--dest',service,'--object-path','/StatusNotifierItem','--method','org.freedesktop.DBus.Properties.Get','org.kde.StatusNotifierItem','Id'],text=True,timeout=5)
        assert 'org.domed.Discord' in identity,identity
        menu=subprocess.check_output(['gdbus','call','--session','--dest',service,'--object-path','/MenuBar','--method','com.canonical.dbusmenu.GetLayout','--','0','-1','[]'],text=True,timeout=5)
        assert 'Quit' in menu and 'Open' in menu,menu
        def universal(method,*args):
            return subprocess.check_output(['gdbus','call','--session','--dest',service,'--object-path','/ApplicationMenu','--method','com.canonical.dbusmenu.'+method,'--',*args],text=True,timeout=5)
        exported=wait(lambda:(value if 'Voice' in (value:=universal('GetLayout','0','-1','[]')) else None))
        for label in ['File','Edit','View','Voice','Window','Help','Preferences','Cut','Copy','Paste','Select All']:
            assert label in exported,label
        assert 'Quit' not in exported,'Quit belongs to the desktop application menu'
        assert 'Close Window' in universal('GetLayout','1','-1','[]')
        assert 'Preferences' in universal('GetLayout','3','-1','[]')
        assert 'false' in universal('GetProperty','18','enabled'),'Disconnected voice commands must be disabled'
        assert 'Control' in universal('GetProperty','12','shortcut')
        wire=(out/'client.log').read_text()
        assert 'org_kde_kwin_appmenu_manager' in wire and 'set_address' in wire and '/ApplicationMenu' in wire
        # Real text input, then a remote Edit > Select All command. The replacement
        # must shrink the selected bitmap span from three characters to one.
        from Xlib import XK
        from Xlib.ext import xtest
        xd=display.Display(screen);host=xd.create_resource_object('window',host.id)
        host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync();time.sleep(.15)
        def type_text(text):
            for character in text:
                code=xd.keysym_to_keycode(XK.string_to_keysym(character))
                xtest.fake_input(xd,X.KeyPress,code);xtest.fake_input(xd,X.KeyRelease,code)
            xd.sync();time.sleep(.2)
        def field_image():
            raw=host.get_image(0,0,g.width,g.height,X.ZPixmap,0xffffffff)
            image=Image.frombytes('RGB',(g.width,g.height),raw.data,'raw','BGRX')
            r=window['desktop']
            return image.crop((round(r['x']+100),round(r['y']+100),round(r['x']+620),round(r['y']+300)))
        def changed(a,b):return sum(pixel!=(0,0,0) for pixel in ImageChops.difference(a,b).getdata())
        type_text('abc');before=field_image()
        universal('Event','14','clicked','<0>','0');time.sleep(.2)
        selected=changed(before,field_image());assert selected>80,('Select All did not select text',selected)
        type_text('d');before=field_image()
        universal('Event','14','clicked','<0>','0');time.sleep(.2)
        replacement=changed(before,field_image())
        assert 20<replacement<selected*.75,('Selected text was not replaced',selected,replacement)
        xd.close()
        # Invoke through D-Bus and observe the client's persisted preference, independently of its rendering.
        universal('Event','15','clicked','<0>','0')
        preference=wait(lambda:next(runtime.glob('data/maicord/preferences.bin'),None))
        wait(lambda:preference.read_bytes()[74]>100)
        universal('Event','17','clicked','<0>','0');wait(lambda:preference.read_bytes()[74]==100)

        # Default Close Window hides to tray; another launch must reveal it.
        universal('Event','7','clicked','<0>','0');time.sleep(.2)
        assert app.poll() is None,'Close Window should preserve the tray client'
        # A second launch uses the upstream single-instance channel.
        second=start([str(ROOT.parent/'maicord-dome-build/MaiCord')],native,'second')
        assert second.wait(timeout=10)==0
        assert len([w for w in state()['windows'] if w['app_id']=='org.domed.Discord'])==1
        quit_id=re.search(r"\((\d+), \{[^{}]*'label': <'Quit",menu).group(1)
        subprocess.check_output(['gdbus','call','--session','--dest',service,'--object-path','/MenuBar','--method','com.canonical.dbusmenu.Event','--',quit_id,'clicked','<0>','0'],text=True,timeout=5)
        assert app.wait(timeout=15)==0
        report={'passed':True,'app_id':window['app_id'],'bitmap_fonts':records,'tray_registered':True,'tray_menu':True,'universal_menu_associated':True,'menu_edit_selection_and_replace':True,'light_text_contrast':True if settings.get('light') else None,'menu_zoom_action':True,'menu_convention':True,'menu_close_action':True,'tray_quit_action':True,'single_instance':True,'clean_exit':True,'real_account_tested':False,'headset_tested':False}
        (out/('ui-light.json' if settings.get('light') else 'ui.json')).write_text(json.dumps(report,indent=2)+'\n')
        print('PASS: native DomeD window, bitmap fonts, tray, associated universal menu, menu convention/close/zoom, single instance')
finally:
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try:child.wait(timeout=8)
            except subprocess.TimeoutExpired:child.kill();child.wait()
    for stream in logs:stream.close()
