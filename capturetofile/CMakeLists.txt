cmake_minimum_required(VERSION 2.8)
project( CaptureToFile )
find_package( OpenCV REQUIRED )
add_executable( capturetofile capturetofile.cpp )
target_link_libraries( capturetofile ${OpenCV_LIBS} )
