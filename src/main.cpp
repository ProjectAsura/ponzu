﻿//-----------------------------------------------------------------------------
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
    desc.Width              = 1920;//2560;
    desc.Height             = 1080;//1440;
    desc.FPS                = 59.9;
    desc.AnimationTimeSec   = 10.0;
    //desc.Path               = "../res/scene/rtcamp.scn";
    desc.Path               = "../res/scene/test_scene.scn";

    r3d::Renderer app(desc);
    app.Run();

    return 0;
}