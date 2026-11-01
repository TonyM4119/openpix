# Connecting OpenPix to other systems

## Home Assistant
Settings → Devices & services → Add integration → **WLED** → enter the controller's IP.
OpenPix speaks the WLED JSON API (`/json/state`, `/json/info`), so you get on/off,
brightness, effect, palette, speed, intensity, color and presets.

## Google Home
Google has no local "pretend to be a bulb" option, so go through Home Assistant:
1. Add OpenPix to Home Assistant (above).
2. Install a Matter bridge add-on for Home Assistant and expose the OpenPix light.
3. In Google Home: Add → Matter device → scan the bridge's pairing code.
Then "Hey Google, turn on the Christmas lights".

## Alexa
Integrations → Alexa **On** → Save → "Alexa, discover devices".
OpenPix answers like a Philips Hue bridge with one dimmable light (local, no skill).

## GE Cync bulbs
Add them to Home Assistant with the official **Cync** integration (needs at least one
Wi-Fi Cync device). Then an automation can mirror OpenPix:

```yaml
automation:
  - alias: Cync bulbs follow OpenPix
    trigger:
      - platform: state
        entity_id: light.alphapix_openpix
    action:
      - choose:
          - conditions: "{{ is_state('light.alphapix_openpix', 'on') }}"
            sequence:
              - service: light.turn_on
                target: { entity_id: light.cync_porch }
                data:
                  rgb_color: "{{ state_attr('light.alphapix_openpix', 'rgb_color') }}"
                  brightness: "{{ state_attr('light.alphapix_openpix', 'brightness') }}"
        default:
          - service: light.turn_off
            target: { entity_id: light.cync_porch }
```
The WLED light's color is the main color of whatever effect is playing
(orange for Halloween, red for Christmas, ...).

## Music
OpenPix receives WLED **audio sync** (UDP multicast 239.0.0.1:11988, V2 packets) from a
WLED sound-reactive device or an audio-sync sender. Turn Music on, pick a 🎵 effect.

## Sync with other WLED / OpenPix
WLED UDP notifier on port 21324, sync group 1: on/off, brightness and the solid color.

## MQTT
Subscribes: `<topic>` (ON / OFF / T / 0-255), `<topic>/col` (#RRGGBB), `<topic>/api` (WLED JSON).
Publishes (retained): `<topic>/g`, `<topic>/c`, `<topic>/status` (online/offline).

## xLights
xLights finds the controller with Art-Net discovery. Use sACN (E1.31), Art-Net or DDP.
