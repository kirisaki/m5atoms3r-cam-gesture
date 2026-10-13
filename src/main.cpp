#include "camera_app.h"

namespace {
CameraApp app;
}

void setup() { app.begin(); }
void loop() { app.tick(); }
