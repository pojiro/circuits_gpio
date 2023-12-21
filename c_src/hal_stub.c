// SPDX-FileCopyrightText: 2018 Frank Hunleth, Mark Sebald, Matt Ludwigs
//
// SPDX-License-Identifier: Apache-2.0

#include "gpio_nif.h"
#include <string.h>

#define NUM_GPIOS 64

/**
 * The stub hardware abstraction layer is suitable for some unit testing.
 *
 * gpiochip0 -> 32 GPIOs. GPIO 0 is connected to GPIO 1, 2 to 3, and so on.
 * gpiochip1 -> 32 GPIOs. GPIO 0 is connected to GPIO 1, 2 to 3, and so on.
 */

struct stub_priv {
    int value[NUM_GPIOS / 2];
    ErlNifPid pid[NUM_GPIOS];
    enum trigger_mode mode[NUM_GPIOS];
};

ERL_NIF_TERM hal_info(ErlNifEnv *env, void *hal_priv, ERL_NIF_TERM info)
{
    (void) hal_priv;

    enif_make_map_put(env, info, atom_name, enif_make_atom(env, "stub"), &info);
    return info;
}

size_t hal_priv_size(void)
{
    return sizeof(struct stub_priv);
}

int hal_load(void *hal_priv)
{
    memset(hal_priv, 0, sizeof(struct stub_priv));
    return 0;
}

void hal_unload(void *hal_priv)
{
    (void) hal_priv;
}

int hal_open_gpio(struct gpio_pin *pin,
                  char *error_str,
                  ErlNifEnv *env)
{
    int pin_base;

    if (strcmp(pin->gpiochip, "gpiochip0") == 0) {
        pin_base = 0;
    } else if (strcmp(pin->gpiochip, "gpiochip1") == 0) {
        pin_base = 32;
    } else {
        strcpy(error_str, "open_failed");
        return -1;
    }

    if (pin->offset < 0 || pin->offset >= 32) {
        strcpy(error_str, "invalid_pin");
        return -1;
    }
    pin->pin_number = pin_base + pin->offset;
    pin->fd = pin->pin_number;

    if (pin->config.is_output && pin->config.initial_value != -1)
        hal_write_gpio(pin, pin->config.initial_value, env);

    *error_str = '\0';
    return 0;
}

void hal_close_gpio(struct gpio_pin *pin)
{
    if (pin->fd >= 0 && pin->fd < NUM_GPIOS) {
        struct stub_priv *hal_priv = pin->hal_priv;
        hal_priv->mode[pin->pin_number] = TRIGGER_NONE;
        pin->fd = -1;
    }
}

static int gpio_value(struct gpio_pin * pin)
{
    struct stub_priv *hal_priv = pin->hal_priv;
    return hal_priv->value[pin->pin_number / 2];
}

int hal_read_gpio(struct gpio_pin *pin)
{
    return gpio_value(pin);
}

static void maybe_send_notification(ErlNifEnv *env, struct stub_priv *hal_priv, int pin_number)
{
    int value = hal_priv->value[pin_number / 2];
    int sendit = 0;
    switch (hal_priv->mode[pin_number]) {
    case TRIGGER_BOTH:
        sendit = 1;
        break;
    case TRIGGER_FALLING:
        sendit = (value == 0);
        break;
    case TRIGGER_RISING:
        sendit = (value != 0);
        break;
    case TRIGGER_NONE:
        sendit = 0;
        break;
    }

    if (sendit) {
        ErlNifTime now = enif_monotonic_time(ERL_NIF_NSEC);
        send_gpio_message(env, enif_make_atom(env, "circuits_gpio2"), pin_number, &hal_priv->pid[pin_number], now, value);
    }
}

int hal_write_gpio(struct gpio_pin *pin, int value, ErlNifEnv *env)
{
    struct stub_priv *hal_priv = pin->hal_priv;
    int half_pin = pin->pin_number / 2;
    if (hal_priv->value[half_pin] != value) {
        hal_priv->value[half_pin] = value;
        maybe_send_notification(env, hal_priv, half_pin * 2);
        maybe_send_notification(env, hal_priv, half_pin * 2 + 1);
    }
    return 0;
}

int hal_apply_interrupts(struct gpio_pin *pin, ErlNifEnv *env)
{
    struct stub_priv *hal_priv = pin->hal_priv;

    hal_priv->mode[pin->pin_number] = pin->config.trigger;
    hal_priv->pid[pin->pin_number] = pin->config.pid;

    maybe_send_notification(env, hal_priv, pin->pin_number);

    return 0;
}

int hal_apply_direction(struct gpio_pin *pin)
{
    (void) pin;
    return 0;
}

int hal_apply_pull_mode(struct gpio_pin *pin)
{
    (void) pin;
    return 0;
}

ERL_NIF_TERM hal_enum(ErlNifEnv *env, void *hal_priv)
{
    ERL_NIF_TERM gpio_list = enif_make_list(env, 0);

    ERL_NIF_TERM chip_label = make_string_binary(env, "stub");
    ERL_NIF_TERM chip_name0 = make_string_binary(env, "gpiochip0");
    ERL_NIF_TERM chip_name1 = make_string_binary(env, "gpiochip1");


    int j;
    for (j = NUM_GPIOS - 1; j >= 0; j--) {
        char line_name[32];
        sprintf(line_name, "pair_%d_%d", j / 2, j % 2);

        ERL_NIF_TERM chip_name = (j >= 32) ? chip_name1 : chip_name0;
        ERL_NIF_TERM line_map = enif_make_new_map(env);
        ERL_NIF_TERM line_label = make_string_binary(env, line_name);
        ERL_NIF_TERM line_offset = enif_make_int(env, j % 32);

        enif_make_map_put(env, line_map, atom_struct, atom_circuits_gpio_line, &line_map);
        enif_make_map_put(env, line_map, atom_controller, chip_name, &line_map);
        enif_make_map_put(env, line_map, atom_label, enif_make_tuple2(env, chip_label, line_label), &line_map);
        enif_make_map_put(env, line_map, atom_line_spec, enif_make_tuple2(env, chip_name, line_offset), &line_map);
        gpio_list = enif_make_list_cell(env, line_map, gpio_list);
    }

    return gpio_list;
}
