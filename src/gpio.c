#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "gpio.h"

LOG_MODULE_REGISTER(gpio, LOG_LEVEL_INF);

#define BUTTON_NODE DT_NODELABEL(button)
#define POWER_KILL_PIN DT_NODELABEL(power_kill_pin)

#define GREEN_LED_PWM_PCT 100U
#define AMBER_LED_PWM_PCT 30U
#define TEMP_MILD 20U

enum led_colour {
	LED_OFF,
	LED_GREEN,
	LED_AMBER,
};

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
static void button_released(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
static void led_timer_timeout_cb(struct k_timer *timer);
static void long_button_press_timeout_cb(struct k_timer *timer);
static void long_button_press_cb(struct k_work *work);

K_TIMER_DEFINE(led_timer, led_timer_timeout_cb, NULL);
K_TIMER_DEFINE(long_button_press_timer, long_button_press_timeout_cb, NULL);
K_WORK_DEFINE(long_button_press_work, long_button_press_cb);

static const struct gpio_dt_spec button_node = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static const struct pwm_dt_spec green_pwm_led = PWM_DT_SPEC_GET(DT_NODELABEL(green_led_pwm));
static const struct pwm_dt_spec amber_pwm_led = PWM_DT_SPEC_GET(DT_NODELABEL(yellow_led_pwm));
static const struct device *pwm_leds_dev = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(green_led_pwm)));
static const struct device *temp_dev = DEVICE_DT_GET_ANY(nordic_nrf_temp);
static struct gpio_callback button_cb_data;
static const struct gpio_dt_spec pwr_kill_pin = GPIO_DT_SPEC_GET(POWER_KILL_PIN, gpios);

/**************************************************************************************************
 * FUNCTIONS
 **************************************************************************************************/

static uint8_t get_green_led_pwm(void)
{
	return GREEN_LED_PWM_PCT;
}

static uint8_t get_amber_led_pwm(void)
{
	return AMBER_LED_PWM_PCT;
}

static void configure_leds(void)
{
	if (!device_is_ready(pwm_leds_dev)) {
		LOG_ERR("Device %s is not ready", pwm_leds_dev->name);
		return;
	}
	if (!device_is_ready(amber_pwm_led.dev)) {
		LOG_ERR("Device %s is not ready", amber_pwm_led.dev->name);
		return;
	}
	if (!device_is_ready(green_pwm_led.dev)) {
		LOG_ERR("Device %s is not ready", green_pwm_led.dev->name);
		return;
	}
}

static void configure_button_interrupt(void(*button_callback), bool rising_edge)
{
	int ret;
	if (rising_edge) {
		ret = gpio_pin_interrupt_configure_dt(&button_node, GPIO_INT_EDGE_RISING);
	} else {
		ret = gpio_pin_interrupt_configure_dt(&button_node, GPIO_INT_EDGE_FALLING);
	}
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure interrupt on %s pin %d", ret, button_node.port->name,
				button_node.pin);
		return;
	}
	gpio_init_callback(&button_cb_data, button_callback, BIT(button_node.pin));
	gpio_add_callback(button_node.port, &button_cb_data);
}

static void configure_button(const struct gpio_dt_spec *button)
{
	int ret;
	ret = gpio_pin_configure_dt(button, GPIO_INPUT);
	if (ret != 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d", ret, button->port->name, button->pin);
		return;
	}
	if (!device_is_ready(button->port)) {
		LOG_ERR("Error: button device %s is not ready", button->port->name);
		return;
	}
	/* Wearable button pin is ACTIVE_HIGH */
	configure_button_interrupt(button_pressed, true);
}

void gpio_init(void)
{
	configure_button(&button_node);
	configure_leds();

	if (temp_dev == NULL || !device_is_ready(temp_dev)) {
		LOG_WRN("no temperature device");
	} else {
		LOG_INF("temp device is %p, name is %s", temp_dev, temp_dev->name);
	}
}

int gpio_green_led_on(void)
{
	int err = led_set_brightness(pwm_leds_dev, green_pwm_led.channel, get_green_led_pwm());
	if (err != 0) {
		LOG_ERR("Failed to set green PWM LED brightness to %d PWM.", get_green_led_pwm());
	} else {
		LOG_DBG("Green PWM LED on");
	}
	return err;
}

int gpio_green_led_off(void)
{
	int err = led_off(pwm_leds_dev, green_pwm_led.channel);
	if (err != 0) {
		LOG_ERR("Failed to turn off PWM LED");
	} else {
		LOG_DBG("Green PWM LED off");
	}
	return err;
}

int gpio_amber_led_on(void)
{
	int err = led_set_brightness(pwm_leds_dev, amber_pwm_led.channel, get_amber_led_pwm());
	if (err != 0) {
		LOG_ERR("Failed to set amber PWM LED brightness to %d PWM.", get_amber_led_pwm());
	} else {
		LOG_DBG("Amber PWM LED on");
	}
	return err;
}

int gpio_amber_led_off(void)
{
	int err = led_off(pwm_leds_dev, amber_pwm_led.channel);
	if (err != 0) {
		LOG_ERR("Failed to turn off amber PWM LED");
	} else {
		LOG_DBG("Amber PWM LED off");
	}
	return err;
}

double gpio_get_temp(void)
{
	struct sensor_value temp_value;

	int err = sensor_sample_fetch(temp_dev);
	if (err) {
		LOG_INF("sensor_sample_fetch failed return: %d", err);
	}

	err = sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &temp_value);
	if (err) {
		LOG_INF("sensor_channel_get failed return: %d", err);
	}

	double temp = sensor_value_to_double(&temp_value);
	LOG_INF("temperature is %gC", temp);

	return temp;
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	LOG_INF("Button pressed");

	if (gpio_get_temp() >= TEMP_MILD) {
		gpio_green_led_on();
	} else {
		gpio_amber_led_on();
	}

	k_timer_start(&long_button_press_timer, K_SECONDS(18), K_NO_WAIT);
	k_timer_start(&led_timer, K_SECONDS(5), K_NO_WAIT);

	configure_button_interrupt(button_released, false);
}

static void button_released(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	k_timer_stop(&long_button_press_timer);

	configure_button_interrupt(button_pressed, true);
}

static void led_timer_timeout_cb(struct k_timer *timer)
{
	(void)gpio_green_led_off();
	(void)gpio_amber_led_off();
}

static void poweroff(void)
{
	LOG_INF("Powering system off");

	int err = gpio_pin_configure_dt(&pwr_kill_pin, GPIO_OUTPUT_ACTIVE);
	if (err != 0) {
		LOG_ERR("Failed to set power kill signal, aborting power off. Error %d", err);
	}
}

static void long_button_press_timeout_cb(struct k_timer *timer)
{
	int err = k_work_submit(&long_button_press_work);
	if (err < 0) {
		LOG_ERR("Failed to submit work of executing long button press event, error %d", err);
	}
}

static void long_button_press_cb(struct k_work *work)
{
	poweroff();
}