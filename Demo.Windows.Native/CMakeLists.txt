
add_executable (YSEHello
        basePage.cpp
        Demo00_PlaySound.cpp
        Demo01_SoundProperties.cpp
        Demo02_3D.cpp
        Demo03_Virtual.cpp
        Demo04_Channels.cpp
        Demo05_Reverb.cpp
        Demo06_Devices.cpp
        Demo07_DspSource.cpp
        Demo08_Occlusion.cpp
        Demo09_Streaming.cpp
        Demo10_FilePosition.cpp
        Demo11_VirtualIO.cpp
        Demo12_AudioTest.cpp
        Demo13_Patcher.cpp
        Demo14_LoadPatcher.cpp
        Demo15_RestartAudio.cpp
        Demo.Windows.Native.cpp
        MenuAction.cpp
        MenuBasics.cpp
        MenuDsp.cpp
        MenuOther.cpp
        MenuTop.cpp
)

link_directories(../build/YseEngine/)

find_package (Threads)

target_link_libraries (YSEHello -lsndfile -lportaudio ${CMAKE_THREAD_LIBS_INIT} -lpthread ${CMAKE_SOURCE_DIR}/build/YseEngine/libYSE_dlib.so)

