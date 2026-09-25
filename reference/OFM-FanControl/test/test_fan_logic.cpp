#include <unity.h>
#include "Fan.h"
#include "MaicoPPB30.h"
#include "IFanHardware.h"
#include <map>
#include <string>

// Mock Hardware Implementation
class MockFanHardware : public IFanHardware {
public:
    struct CallLog {
        std::string method;
        int p1, p2;
    };
    std::vector<CallLog> logs;
    
    std::map<uint8_t, int16_t> pwmValues;
    std::map<uint8_t, bool> digitalValues;
    
    std::function<void()> directionCallback;
    long directionInterval = 0;
    bool directionTimerRunning = false;

    void init(uint8_t s1, uint8_t s2, uint8_t sw) override {
        logs.push_back({"init", s1, s2});
    }

    void setPWM(uint8_t pin, int16_t value) override {
        pwmValues[pin] = value;
        logs.push_back({"setPWM", pin, value});
    }

    void setDigital(uint8_t pin, bool value) override {
        digitalValues[pin] = value;
        logs.push_back({"setDigital", pin, (int)value});
    }

    void startDirectionTimer(long intervalMs, std::function<void()> callback) override {
        directionInterval = intervalMs;
        directionCallback = callback;
        directionTimerRunning = true;
        logs.push_back({"startDirectionTimer", (int)intervalMs, 0});
    }

    void stopDirectionTimer() override {
        directionTimerRunning = false;
        logs.push_back({"stopDirectionTimer", 0, 0});
    }

    void startOneShotTimer(long delayMs, std::function<void()> callback) override {
        logs.push_back({"startOneShotTimer", (int)delayMs, 0});
        // Auto-fire for simplicity in synchronous tests if needed, or store to fire manually
    }

    void stopOneShotTimer() override {
        logs.push_back({"stopOneShotTimer", 0, 0});
    }

    void printLogs() {
        printf("MockFanHardware Logs:\n");
        for (const auto& log : logs) {
            printf("  %s(%d, %d)\n", log.method.c_str(), log.p1, log.p2);
        }
    }
};

void test_fan_initialization() {
    MockFanHardware mockHw;
    MaicoPPB30 fan(mockHw, 1, 2, 3);
    
    // Check if init was called (init + 2 setPWMs)
    TEST_ASSERT_EQUAL(3, mockHw.logs.size());
    TEST_ASSERT_EQUAL_STRING("init", mockHw.logs[0].method.c_str());
    
    // Check initial speed 0
    TEST_ASSERT_EQUAL(0, fan.getFanSpeed());
}

void test_fan_speed_control() {
    MockFanHardware mockHw;
    MaicoPPB30 fan(mockHw, 1, 2, 3);
    mockHw.logs.clear();

    fan.setFanSpeed(3);
    TEST_ASSERT_EQUAL(3, fan.getFanSpeed());
    
    
    fan.setFanSpeed(0);
    TEST_ASSERT_EQUAL(0, fan.getFanSpeed());
}

void test_operating_mode_logic() {
    MockFanHardware mockHw;
    MaicoPPB30 fan(mockHw, 1, 2, 3);

    fan.setOperatingMode(Fan::OperatingMode::Off);
    // Check SW pin set to LOW (false)
    TEST_ASSERT_FALSE(mockHw.digitalValues[3]);

    fan.setOperatingMode(Fan::OperatingMode::Manual);
    // Check SW pin set to HIGH (true)
    TEST_ASSERT_TRUE(mockHw.digitalValues[3]);

    fan.setOperatingMode(Fan::OperatingMode::Automatic);
    fan.setControlMode(Fan::ControlMode::Threshold);
    // Check SW pin set to HIGH (true)
    TEST_ASSERT_TRUE(mockHw.digitalValues[3]);
    fan.setInsideHumdity(80.0); // Above default threshold
    TEST_ASSERT_GREATER_THAN(0, fan.getFanSpeed()); // Should turn on
    // Check fan speed is 0 after switching to manual from automatic
    fan.setOperatingMode(Fan::OperatingMode::Manual);
    TEST_ASSERT_EQUAL(0, fan.getFanSpeed());
}  

void test_automatic_mode_logic() {
    MockFanHardware mockHw;
    MaicoPPB30 fan(mockHw, 1, 2, 3);
    
    fan.setOperatingMode(Fan::OperatingMode::Automatic);
    fan.setVentilationMode(Fan::VentilationMode::HeatRecovery, Fan::VentilationModeTarget_Manual);
    fan.setVentilationMode(Fan::VentilationMode::ExhaustAir, Fan::VentilationModeTarget_Automatic);
    fan.setInsideTemperature(20.0);
    fan.setInsideHumdity(50.0); // Below threshold (75)
    fan.thresholdHumidityOff = 60; // Set hysteresis
    fan.thresholdHumidityOn = 66;
    
    // Should be off
    TEST_ASSERT_EQUAL(0, fan.getFanSpeed());
    TEST_ASSERT_EQUAL(Fan::VentilationMode::HeatRecovery, fan.getVentilationMode()); // Should be in heat recovery mode

    // Increase humidity above threshold
    fan.setInsideHumdity(80.0);
    fan.setInsideHumdity(70.0);
    // Should turn on to threshold speed (default 4)
    TEST_ASSERT_EQUAL(4, fan.getFanSpeed());
    TEST_ASSERT_EQUAL(Fan::VentilationMode::ExhaustAir, fan.getVentilationMode()); // Should be in exhaust air mode
    // Decrease humidity to upper hysteresis value
    fan.setInsideHumdity(66.0);
    TEST_ASSERT_EQUAL(4, fan.getFanSpeed());
    // Decrease humidity to in between hysteresis value
    fan.setInsideHumdity(63.0);
    TEST_ASSERT_EQUAL(4, fan.getFanSpeed());
    // Decrease humidity to lower hysteresis value
    fan.setInsideHumdity(60.0);
    TEST_ASSERT_EQUAL(4, fan.getFanSpeed());
    // Decrease humidity below threshold
    fan.setInsideHumdity(50.0);
    // Should turn off
    TEST_ASSERT_EQUAL(0, fan.getFanSpeed());
    TEST_ASSERT_EQUAL(Fan::VentilationMode::HeatRecovery, fan.getVentilationMode()); // Should be back to heat recovery mode


    fan.thresholdHumidityOff = 66; // negative hysteresis
    fan.thresholdHumidityOn = 60;
    // Increase humidity above threshold
    fan.setInsideHumdity(80.0);
    // Should turn on to threshold speed (default 4)
    TEST_ASSERT_EQUAL(4, fan.getFanSpeed());
    // Decrease humidity below threshold
    fan.setInsideHumdity(50.0);
    // Should turn off
    TEST_ASSERT_EQUAL(0, fan.getFanSpeed());
}

void test_heat_recovery_timer() {
    MockFanHardware mockHw;
    MaicoPPB30 fan(mockHw, 1, 2, 3);
    
    fan.setVentilationMode(Fan::VentilationMode::HeatRecovery);
    fan.setFanSpeed(3); 
    
    // Should start timer
    TEST_ASSERT_TRUE(mockHw.directionTimerRunning);
    TEST_ASSERT_EQUAL(60000, mockHw.directionInterval); // 60s default
    
    // Simulate timer fire
    if(mockHw.directionCallback) {
        mockHw.directionCallback();
    }
    
    // Directions should have flipped. Hard to verify internal state directly without getters, 
    // but we can check if setPWM was called again with flipped values if we calculated them.
    // For now, just verifying the timer interaction is good.
}

void test_threshold_crossing_detection() {
    MockFanHardware mockHw;
    MaicoPPB30 fan(mockHw, 1, 2, 3);
    
    fan.thresholdHumidityOff = 60;
    fan.thresholdHumidityOn = 66;

    // Initially below threshold
    fan.setInsideHumdity(40.0);
    // No crossing yet
    bool crossed = fan.setInsideHumdity(42.0);
    TEST_ASSERT_FALSE(crossed);
    
    // Cross threshold upwards
    crossed = fan.setInsideHumdity(80.0);
    TEST_ASSERT_TRUE(crossed);
    crossed = fan.setInsideHumdity(78.0);
    TEST_ASSERT_FALSE(crossed);

    // Cross threshold downwards
    crossed = fan.setInsideHumdity(50.0);
    TEST_ASSERT_TRUE(crossed);
    crossed = fan.setInsideHumdity(52.0);
    TEST_ASSERT_FALSE(crossed);
}

void test_manual_override() {
    MockFanHardware mockHw;
    MaicoPPB30 fan(mockHw, 1, 2, 3);
    
    // Switch to Automatic
    fan.setOperatingMode(Fan::OperatingMode::Automatic);
    fan.setControlMode(Fan::ControlMode::Threshold);
    fan.setInsideHumdity(80.0); // Above threshold
    TEST_ASSERT_EQUAL(fan.thresholdSpeed, fan.getFanSpeed()); // Should turn on automatically
    
    // Now manually override
    fan.setFanSpeed(2);
    TEST_ASSERT_EQUAL(2, fan.getFanSpeed());
    // Change humidity, should NOT change speed due to manual override
    fan.setInsideHumdity(90.0);
    TEST_ASSERT_EQUAL(2, fan.getFanSpeed());

    // Now change humidity again, should return to manual override speed
    fan.setInsideHumdity(40.0);
    TEST_ASSERT_EQUAL(2, fan.getFanSpeed());

    // Now manually override
    fan.setFanSpeed(1);
    TEST_ASSERT_EQUAL(1, fan.getFanSpeed());
    // Now change humidity again, should NOT change speed due to manual override
    fan.setInsideHumdity(38.0);

    // Reset manual override by changing operating mode
    fan.setOperatingMode(Fan::OperatingMode::Automatic);
    fan.setInsideHumdity(80.0);
    TEST_ASSERT_EQUAL(fan.thresholdSpeed, fan.getFanSpeed());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_fan_initialization);
    RUN_TEST(test_fan_speed_control);
    RUN_TEST(test_operating_mode_logic);
    RUN_TEST(test_automatic_mode_logic);
    RUN_TEST(test_heat_recovery_timer);
    RUN_TEST(test_threshold_crossing_detection);
    RUN_TEST(test_manual_override);
    UNITY_END();
    return 0;
}
