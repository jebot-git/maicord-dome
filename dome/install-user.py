#!/usr/bin/env python3
"""Install the prepared native fork for the current user; does not replace the official client."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
ROOT=Path(__file__).resolve().parent.parent
binary=ROOT.parent/'maicord-dome-build/MaiCord'
if not binary.is_file():raise SystemExit('Run bash dome/build.sh first.')
subprocess.run(['python3',str(ROOT/'tools/audit_image.py'),str(binary),'--no-test-hooks'],check=True)
data=Path(os.environ.get('XDG_DATA_HOME',str(Path.home()/'.local/share')))
base=Path.home()/'.local/lib/dome-discord';base.mkdir(parents=True,exist_ok=True)
(base/'libexec').mkdir(exist_ok=True)
# os.replace avoids truncating a running executable.
temporary=base/'libexec/MaiCord.next';shutil.copy2(binary,temporary);temporary.chmod(0o755);temporary.replace(base/'libexec/MaiCord')
shutil.copytree(ROOT/'dome',base/'dome',dirs_exist_ok=True,ignore=shutil.ignore_patterns('validation','__pycache__'))
shutil.copytree(binary.parent/'licenses',base/'licenses',dirs_exist_ok=True)
shutil.copy2(ROOT/'DOMED.md',base/'DOMED.md')
# The installed launcher has the same relative layout as the checkout.
run=base/'dome/run.sh'
run.write_text('#!/usr/bin/env bash\nexec python3 '+shlex.quote(str(base/'dome/launch.py'))+' --binary '+shlex.quote(str(base/'libexec/MaiCord'))+' "$@"\n');run.chmod(0o755)
bin_dir=Path.home()/'.local/bin';bin_dir.mkdir(exist_ok=True)
wrapper=bin_dir/'dome-discord'
wrapper.write_text('#!/usr/bin/env bash\nexec '+shlex.quote(str(run))+' "$@"\n');wrapper.chmod(0o755)
applications=data/'applications';applications.mkdir(parents=True,exist_ok=True)
desktop=(ROOT/'dome/org.domed.Discord.desktop').read_text().replace('Exec=dome-discord %U','Exec="'+str(wrapper).replace('"','\\"')+'" %U')
(applications/'org.domed.Discord.desktop').write_text(desktop)
icons=data/'icons/hicolor/scalable/apps';icons.mkdir(parents=True,exist_ok=True)
shutil.copy2(ROOT/'dome/org.domed.Discord.svg',icons/'org.domed.Discord.svg')
if shutil.which('update-desktop-database'):subprocess.run(['update-desktop-database',str(applications)],check=True)
print('Installed DomeD Discord:',wrapper)
