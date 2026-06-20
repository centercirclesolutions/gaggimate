#ifndef TEMPERATURESENSOR_H
#define TEMPERATURESENSOR_H

constexpr double MAX_SAFE_TEMP = 170.0;

class TemperatureSensor {
  public:
    virtual ~TemperatureSensor() = default;
    virtual float read() { return 0.0f; }
    virtual bool isErrorState() { return false; }
    virtual void setup() {}
};

#endif // TEMPERATURESENSOR_H
