 
//Version to use esp timer only
//Interrupts for both trigger and echo
//One shot timer for trigger
//GPIO interrupts for echo
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"   //Using esp timer for trigger and echo

#define TRIG GPIO_NUM_11
#define ECHO GPIO_NUM_12
#define LOOP_DELAY_MS 1000
#define GPTIMER_RESOLUTION_HZ  1000000 // 1MHz, 1 tick = 1us
#define OUT_OF_RANGE_SHORT 116
#define OUT_OF_RANGE_LONG 23200

static const char *TAG = "example"; //Name used for esp log prints

//Pulse time calculated in ISR
uint64_t echo_pulse_time = 0;

//ISR for the echo pulse
void IRAM_ATTR echo_isr_handler(void* arg) {
    static uint64_t rising_edge_time = 0;
    static uint64_t falling_edge_time = 0;
    if (gpio_get_level(ECHO) == 1) {
        // store the timestamp when pos edge is detected
        rising_edge_time = esp_timer_get_time();
    } else {
        // capture the negative edge time and compute pulse time
        falling_edge_time = esp_timer_get_time();
        echo_pulse_time = (falling_edge_time - rising_edge_time); 
    }
} 

//ISR for the trigger pulse
void IRAM_ATTR oneshot_timer_callback(void* arg) 
{
    gpio_set_level(TRIG, 0);
}
void hc_sr04_init(); // Initialization function for sensor

void app_main(void)
{
    float distance_cm = 0.0;
    //Configure the TRIG and ECHO pins
    hc_sr04_init();
    
    // Create one-shot esp timer for trigger 
    const esp_timer_create_args_t oneshot_timer_args = {
            .callback = &oneshot_timer_callback,
            .name = "one-shot"
    };
    esp_timer_handle_t oneshot_timer;
    ESP_ERROR_CHECK(esp_timer_create(&oneshot_timer_args, &oneshot_timer));
    
    while(1) {
        // Set trigger pin high and start 1-shot 10us timer
        gpio_set_level(TRIG, 1);
        ESP_ERROR_CHECK(esp_timer_start_once(oneshot_timer, 10));

        vTaskDelay(20/portTICK_PERIOD_MS); //wait past echo pulse to calculate distance
         if (echo_pulse_time < OUT_OF_RANGE_SHORT) {
            distance_cm = 0.0; 
        } else {
            if (echo_pulse_time > OUT_OF_RANGE_LONG) {
                distance_cm = 1000.0; //indicates error - distance out of range
            } else {
                distance_cm = echo_pulse_time/58.3;
            } 
        }
        ESP_LOGI(TAG, "Distance in cm: %.1f", distance_cm);
        vTaskDelay(LOOP_DELAY_MS/portTICK_PERIOD_MS); //loop time 1s
    }
}

void hc_sr04_init() {
    //Trigger is an output, initially 0
	gpio_reset_pin(TRIG);
	gpio_set_direction(TRIG, GPIO_MODE_OUTPUT);
	gpio_set_level(TRIG, 0); // Ensure trig is low initially

    // Configure echo to interrupt on both edges. 
    gpio_reset_pin(ECHO);
    gpio_set_direction(ECHO, GPIO_MODE_INPUT);
    gpio_set_intr_type(ECHO, GPIO_INTR_ANYEDGE);
    gpio_intr_enable(ECHO);  //Enable interrupts on ECHO
    gpio_install_isr_service(0);  //Creates global ISR that catches all GPIO interrupts
    
    //Dispatch pin handler for ECHO
    ESP_ERROR_CHECK(gpio_isr_handler_add(ECHO, echo_isr_handler, NULL)); 

}