//-----------------------------------------------------------------------------
// File : main.cpp
// Desc : Main Entry Point.
// Copyright(c) Project Asura. All right reserved.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <RendererApp.h>


//-----------------------------------------------------------------------------
//      メインエントリーポイントです.
//-----------------------------------------------------------------------------
int main(int argc, char** argv)
{
    r3d::SceneDesc desc = {};
    desc.RenderTimeSec      = 255.9;
    desc.OutputWidth        = 1920;
    desc.OutputHeight       = 1080;
    desc.RenderWidth        = 1920;//1920;//2560;
    desc.RenderHeight       = 1080;//1080;//1440;
    desc.FPS                = 24.0;
    desc.AnimationTimeSec   = 10.0;
#if 1
    desc.SceneFilePath      = "../etc/scene/rtcamp_2024.scn";
    desc.CameraFilePath     = "../etc/scene/rtcamp_2024.cam";
#else
    desc.SceneFilePath      = "../etc/scene/test_scene.scn";
    desc.CameraFilePath     = "../etc/scene/test_camera.cam";
#endif
    r3d::Renderer app(desc);
    app.Run();

    return 0;
}