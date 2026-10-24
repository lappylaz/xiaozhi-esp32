# esp-wifi-connect 3.3.1, vendored with one Sabrina patch

Source: `78/esp-wifi-connect` 3.3.1 from the ESP component registry. All 22 files
matched the registry's `CHECKSUMS.json` before vendoring. That file and
`.component_hash` are dropped here because they describe the registry archive,
which the patched copy no longer matches. `main/idf_component.yml` points the
dependency at this directory with `override_path`.

## Patch: the setup page always opens in the device's language

The configuration page chooses its language from, in order: a `lang` query
parameter, the language that browser last saved for the page's origin, then the
browser language. Captive-portal probes are already redirected to `/?lang=<device
language>`. A person who types `http://192.168.4.1` gets no parameter, and every
AiPi hotspot shares that origin, so a language saved months earlier by a
different device's page wins over both the device and the phone. That showed a
Chinese page on an English build and an English phone.

`wifi_configuration_ap.cc`: `GET /` without a `lang` parameter now answers
`302 Found` to `/?lang=<device language>`; a request that has one is served
unchanged. The language menu on the page still works for that visit.

Drop this directory and the `override_path` once upstream applies the device
language to bare visits.
