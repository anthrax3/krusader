set(KViewer_SRCS
    krviewer.cpp
    kimagefilepreview.cpp
    panelviewer.cpp
    diskusageviewer.cpp
    lister.cpp)

add_library(KViewer STATIC ${KViewer_SRCS})

target_link_libraries(KViewer KF5::KHtml)
