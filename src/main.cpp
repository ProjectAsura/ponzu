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
    desc.RenderTimeSec      = 299.0;
    desc.OutputWidth        = 1920;
    desc.OutputHeight       = 1080;
    desc.RenderWidth        = 1920;//1920;//2560;
    desc.RenderHeight       = 1080;//1080;//1440;
    desc.FPS                = 23.9;
    desc.AnimationTimeSec   = 5.0;
    //desc.SceneFilePath      = "../res/scene/rtcamp.scn";
    desc.SceneFilePath      = "../res/scene/test_scene.scn";

    r3d::Renderer app(desc);
    app.Run();

    return 0;
}