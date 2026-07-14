#include "IComputeProgram.hpp"

IComputeProgram::IComputeProgram(CameraController const& camera, Window& window) :
    mCamera{camera},
    mWindow{window},
    mScreenSize{window.getSize()}
{
}