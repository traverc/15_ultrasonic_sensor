 
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"  //Using gp timer for echo
#include "esp_log.h"
#include "esp_timer.h"   //Using esp timer for trigger


#define TRIG GPIO_NUM_11
#define ECHO GPIO_NUM_12
#define LOOP_DELAY_MS 1000
#define GPTIMER_RESOLUTION_HZ  1000000 // 1MHz, 1 tick = 1us

static const char *TAG = "example"; //Name used for esp log prints

//Distance defined as a global
float distance_cm = 0.0;

//Timer for echo configured as a global since it is used by ISR?
gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = GPTIMER_RESOLUTION_HZ, // 1MHz, 1 tick=1us
};

//ISR for the echo pulse
void IRAM_ATTR echo_isr_handler(void* arg) {
    static uint64_t rising_edge_time = 0;
    static uint64_t falling_edge_time = 0;
    static uint64_t echo_pulse_time = 0;
 //Note: added some error checking to this for distances out of spec
 //2 cm - 400 cm - but should be re-done with macros
    if (gpio_get_level(ECHO) == 1) {
        // store the timestamp when pos edge is detected
        gptimer_get_raw_count(gptimer, &rising_edge_time);
    } else {
        // capture the negative edge time and calculate distance
        gptimer_get_raw_count(gptimer, &falling_edge_time);
        echo_pulse_time = (falling_edge_time - rising_edge_time); 
        if ((echo_pulse_time < 116)|| (echo_pulse_time > 23200)) {
            distance_cm = 1000.0; //indicates error - distance out of range
        } else {
        distance_cm = echo_pulse_time/58.0;
        } 
    }
} 

//ISR for the trigger pulse
static void oneshot_timer_callback(void* arg)
{
    ESP_LOGI(TAG, "One-shot timer called, time since boot: %lld us", esp_timer_get_time());
    gpio_set_level(TRIG, 0);
}

void hc_sr04_init(); // Initialization function for sensor

void app_main(void)
{
    //Configure the TRIG and ECHO pins
    hc_sr04_init();
    
    /* Create one-shot esp timer for trigger  - does not need to be global?   */
    const esp_timer_create_args_t oneshot_timer_args = {
            .callback = &oneshot_timer_callback,
            .name = "one-shot"
    };
    esp_timer_handle_t oneshot_timer;
    ESP_ERROR_CHECK(esp_timer_create(&oneshot_timer_args, &oneshot_timer));

    // Create a GP timer instance for echo
    gptimer_new_timer(&timer_config, &gptimer);
    ESP_LOGI(TAG, "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    // Start the timer
    ESP_ERROR_CHECK(gptimer_start(gptimer)); 
    
    while(1) {
        // Send trigger pulse
        ESP_LOGI(TAG, "Send trigger with esp timer");

        gpio_set_level(TRIG, 1);
        ESP_ERROR_CHECK(esp_timer_start_once(oneshot_timer, 10));
        ESP_LOGI(TAG, "Started one-shot timer, time since boot: %lld us", esp_timer_get_time());

        vTaskDelay(20/portTICK_PERIOD_MS); //wait past echo pulse to print distance
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