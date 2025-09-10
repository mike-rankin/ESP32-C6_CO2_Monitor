#include <lvgl.h>
#include <TFT_eSPI.h>
#include <ui.h>
#include <SensirionI2cStcc4.h>
#include <Wire.h>
#include <CST816S.h>
#include <Adafruit_NeoPixel.h>

#define STCC4_I2C_ADDR_64 0x65  //Was 64 by default, For Rev2 change SensirionI2cStcc4.h to 0x65

#define NUMPIXELS 1
#define LED_PIN   8
#define TFT_BL    17 

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0


static char errorMessage[64];
static int16_t error;

// Timing variables for non-blocking sensor reading
unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 5000; // 5 seconds in milliseconds
bool sensorReadInProgress = false;
unsigned long dataReadyCheckStart = 0;

bool useFahrenheit = false;  // false = Celsius, true = Fahrenheit

Adafruit_NeoPixel pixel(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
SensirionI2cStcc4 sensor;
CST816S touch(23, 22, 16, 14);	// sda, scl, rst, int)

/*Change to your screen resolution*/
static const uint16_t screenWidth  = 240;
static const uint16_t screenHeight = 240;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[ screenWidth * screenHeight / 10 ];

TFT_eSPI tft = TFT_eSPI(); /* TFT instance */

#if LV_USE_LOG != 0
/* Serial debugging */
void my_print(const char * buf)
{
    Serial.printf(buf);
    Serial.flush();
}
#endif


// Utility function for printing uint64_t values
void PrintUint64(uint64_t& value) {
    Serial.print("0x");
    Serial.print((uint32_t)(value >> 32), HEX);
    Serial.print((uint32_t)(value & 0xFFFFFFFF), HEX);
}

// LED helper function
void setLED(uint8_t r, uint8_t g, uint8_t b) {
  pixel.clear();
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}



/* Display flushing */
void my_disp_flush( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p )
{
    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );

    tft.startWrite();
    tft.setAddrWindow( area->x1, area->y1, w, h );
    tft.pushColors( ( uint16_t * )&color_p->full, w * h, true );
    tft.endWrite();

    lv_disp_flush_ready( disp );
}

/*Read the touchpad*/
void my_touchpad_read( lv_indev_drv_t * indev_driver, lv_indev_data_t * data )
{
    //uint16_t touchX = 0, touchY = 0;
    //bool touched = false;//tft.getTouch( &touchX, &touchY, 600 );
    bool touched = touch.available();

    if( !touched )
    {
        data->state = LV_INDEV_STATE_REL;
    }
    else
    {
        data->state = LV_INDEV_STATE_PR;

        /*Set the coordinates*/
        data->point.x = touch.data.x;
        data->point.y = touch.data.y;
    }
}



void readSensorData() {
    static bool waitingForData = false;
    
    unsigned long currentTime = millis();
    
    // Check if it's time for a new sensor reading
    if (!sensorReadInProgress && (currentTime - lastSensorRead >= sensorInterval)) {
        sensorReadInProgress = true;
        waitingForData = true;
        dataReadyCheckStart = currentTime;
        Serial.println("Starting sensor read...");
    }
    
    // If we're waiting for data to be ready
    if (waitingForData && sensorReadInProgress) {
        // For continuous measurement mode, we don't need to check data ready status
        // The sensor provides data continuously, so we can read directly after delay
        
        // Wait for measurement to complete (similar to working example)
        if (currentTime - dataReadyCheckStart > 1000) { // Wait 1 second for measurement
            // Data should be ready, read it
            int16_t co2Concentration = 0;
            float temperature = 0.0;
            float relativeHumidity = 0.0;
            uint16_t sensorStatus = 0; // Added status parameter
            
            error = sensor.readMeasurement(co2Concentration, temperature, relativeHumidity, sensorStatus);
            if (error != NO_ERROR) {
                // A failed read can be caused by clock shifting. Retry after delay.
                Serial.print("Error trying to execute readMeasurement() (retry in 150ms): ");
                errorToString(error, errorMessage, sizeof errorMessage);
                Serial.println(errorMessage);
                delay(150);
                
                error = sensor.readMeasurement(co2Concentration, temperature, relativeHumidity, sensorStatus);
                if (error != NO_ERROR) {
                    Serial.print("Error trying to execute readMeasurement() after additional delay: ");
                    errorToString(error, errorMessage, sizeof errorMessage);
                    Serial.println(errorMessage);
                    sensorReadInProgress = false;
                    waitingForData = false;
                    lastSensorRead = currentTime;
                    return;
                }
            }
            
            // Update UI with new sensor data
            updateUIWithSensorData(co2Concentration, temperature, relativeHumidity);
            Serial.println("Sensor data updated");
            
            sensorReadInProgress = false;
            waitingForData = false;
            lastSensorRead = currentTime;
        }
    }
}


// Function to update UI elements with sensor data
void updateUIWithSensorData(uint16_t co2, float temp, float humidity) {
    // Update CO2 value and arc
    setLED(0, 25, 0);
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%u", co2);
    lv_label_set_text(ui_CO2Value, buffer);
    
    // Update arc based on CO2 level
    co2 = constrain(co2, 400, 2000);  // Constrain the CO2 value
    int arcValue = map(co2, 400, 2000, 15, 35);    // Map to arc range
    Serial.print("CO2: ");
    Serial.print(co2);
    Serial.print(" ppm, Arc value: ");
    Serial.println(arcValue);
    lv_arc_set_value(ui_Arc1, arcValue);  // Update the arc
    
    // Update temperature - convert if needed
    char buffer1[10];
    float displayTemp = temp;
    
    if (useFahrenheit) {
        // Convert Celsius to Fahrenheit: F = (C × 9/5) + 32
        displayTemp = (temp * 9.0 / 5.0) + 32.0;
        Serial.print("TempF: ");
        Serial.println(temp);
        lv_label_set_text(ui_TemperatureUnit, "F");  // Set to Fahrenheit
    } else {
        Serial.print("TempC: ");
        Serial.println(temp);
        lv_label_set_text(ui_TemperatureUnit, "C");  // Set to Celsius
    }
    
    snprintf(buffer1, sizeof(buffer1), "%.1f", displayTemp);
    lv_label_set_text(ui_TemperatureValue, buffer1);
    
    // Update humidity
    char buffer2[10];
    snprintf(buffer2, sizeof(buffer2), "%.1f", humidity);
    lv_label_set_text(ui_HumidityValue, buffer2);
}


void setup()
{
    Serial.begin( 115200 ); 
    delay(250);
    pixel.begin();
    setLED(50, 0, 0); delay(500); setLED(0, 50, 0); delay(500); setLED(0, 0, 50); delay(500);
    Wire.begin(23,22);
    sensor.begin(Wire, STCC4_I2C_ADDR_64); 
    touch.begin();


     // Stop any ongoing measurements and get sensor info for CO2 sensor
    uint32_t productId = 0;
    uint64_t serialNumber = 0;
    
    error = sensor.stopContinuousMeasurement();
    if (error != NO_ERROR) {
        Serial.print("Error trying to execute stopContinuousMeasurement(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        return;
    }
    
    error = sensor.getProductId(productId, serialNumber);
    if (error != NO_ERROR) {
        Serial.print("Error trying to execute getProductId(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        return;
    }
    
    Serial.print("productId: ");
    Serial.print(productId);
    Serial.print("\t");
    Serial.print("serialNumber: ");
    PrintUint64(serialNumber);
    Serial.println();
    
    // Start continuous measurement for CO2 sensor
    error = sensor.startContinuousMeasurement();
    if (error != NO_ERROR) {
        Serial.print("Error trying to execute startContinuousMeasurement(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        return;
    } 



    analogWrite(TFT_BL, 100);  //PWM Backlight

    String LVGL_Arduino = "Hello Arduino! ";
    LVGL_Arduino += String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();

    Serial.println( LVGL_Arduino );
    Serial.println( "I am LVGL_Arduino" );

    lv_init();

#if LV_USE_LOG != 0
    lv_log_register_print_cb( my_print ); /* register print function for debugging */
#endif

    tft.begin();          /* TFT init */
    tft.setRotation( 0 ); /* Landscape orientation, flipped */

    lv_disp_draw_buf_init( &draw_buf, buf, NULL, screenWidth * screenHeight / 10 );

    /*Initialize the display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init( &disp_drv );
    /*Change the following line to your display resolution*/
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register( &disp_drv );

    /*Initialize the (dummy) input device driver*/
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init( &indev_drv );
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register( &indev_drv );

    ui_init();

    Serial.println( "Setup done" );
}

void loop()
{
    static int32_t lastSliderValue = 100;  //was -1

    // Non-blocking sensor reading
    readSensorData();

    // Get slider value directly and adjust brightness when it changes
    int32_t currentSliderValue = lv_slider_get_value(ui_BrightnessSlider);

    if (currentSliderValue != lastSliderValue) {
        // Convert slider value (25-100) to PWM value (0-255)
        int brightnessLevel = map(currentSliderValue, 25, 100, 25, 255);
        analogWrite(TFT_BL, brightnessLevel);
        Serial.print(currentSliderValue);
        lastSliderValue = currentSliderValue;
    }

    lv_timer_handler(); 
    delay(5);
}
