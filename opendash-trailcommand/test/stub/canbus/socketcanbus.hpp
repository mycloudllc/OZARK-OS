#pragma once
// Test-build stand-in for Dash's canbus/socketcanbus.hpp. The real header pulls in OpenAuto and
// Qt SerialBus; the vehicle plugin interface only needs ICANBus as an opaque pointer.
class ICANBus;
