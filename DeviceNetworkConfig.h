#pragma once

// Device-network defaults. Change only these four macros when deploying to a
// different device or PC network; the rest of the application reads them
// through RobotEndpoint.
#define DEVICE_IP "106.55.38.224"
#define DEVICE_PORT 8000
#define LOCAL_BIND_IP "0.0.0.0"
#define LOCAL_RECEIVE_PORT 54545
