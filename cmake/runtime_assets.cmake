# The runtime assets both clients carry beside the executable (included by cmake/client.cmake once MaiCord exists):
# the license texts, and the bundled fonts the renderer reads from fonts/.
add_custom_command(TARGET MaiCord POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MaiCord>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opus/COPYING"
        "$<TARGET_FILE_DIR:MaiCord>/licenses/Opus-BSD.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "$<TARGET_FILE_DIR:MaiCord>/licenses/MaiCord-MPL-2.0.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/licenses/Twemoji-graphics.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/licenses/Unicode-3.0.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/licenses/ICU-Thai-dictionary.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/licenses/PyThaiNLP-corpus.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/licenses/gemoji-MIT.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/licenses/emojibase-MIT.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/licenses/MaiCord-sounds.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/licenses/Lucide-ISC.txt"
        "$<TARGET_FILE_DIR:MaiCord>/licenses")
# This fork never ships upstream outline fonts. The launcher builds BDF-derived bitmap strikes.
add_custom_target(maicord_runtime_fonts)
function(maicord_copy_fonts)
endfunction()
