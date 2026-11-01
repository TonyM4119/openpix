# Lights (Android app)

Control an OpenPix controller from your phone: holiday effects, brightness, speed,
lights on/off, test patterns, and firmware updates from this repo's `release/` folder.

Install: open https://github.com/TonyM4119/openpix/releases/latest on your phone,
download **Lights.apk**, open it, and allow installing from your browser if asked.

The app is built by `.github/workflows/android.yml` on every push that touches `app/`,
signed with the key stored in the `KEYSTORE_B64` / `KEYSTORE_PASSWORD` repo secrets.
