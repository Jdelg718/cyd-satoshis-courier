Vendored from espressif/esp-bsp @ components/es8311 (Apache-2.0, see LICENSE).

Local modification: the upstream driver uses the legacy `driver/i2c.h` API
(`i2c_master_write_to_device` / `i2c_port_t`). This project uses the new
`driver/i2c_master.h` bus API for the FT6336G touch controller, and IDF 5.5
forbids both APIs on the same port. The ES8311 shares that bus (IO15/IO16),
so `es8311_create()` now takes an `i2c_master_bus_handle_t` and the two
register-access helpers were reimplemented on `i2c_master_transmit`
/ `i2c_master_transmit_receive`. Clock coefficient tables are untouched.
