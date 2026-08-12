# MotoNav-CYD V0.7 test plan

## 1. Arduino verification

1. Open `MotoNav_CYD_V0_7_GPX_Reliability.ino`.
2. Select `ESP32 Dev Module`.
3. Run **Verify**, then upload the firmware.

## 2. Normal GPX recording

1. Insert a working microSD card and acquire a GNSS fix.
2. Start a track manually or use automatic start above 5 km/h.
3. Confirm `REC N` increases and `PAUSE` appears after 10 seconds without movement.
4. Finish the track from the menu.
5. Open the GPX on a computer and confirm it ends with `</gpx>`.

## 3. Power-loss recovery

1. Start recording and wait for at least 10 points.
2. Disconnect MotoNav power without finishing the track.
3. Restore power with the same microSD card inserted.
4. Confirm `RECOVERED` appears for about 5 seconds.
5. Open the interrupted GPX on a computer and confirm it ends with `</gpx>`.

## 4. SD fault handling

1. Boot without a microSD card and confirm `NO SD`.
2. Confirm the speedometer and trip screen still work.
3. Start a recording with a card inserted, then remove the card while recording.
4. Confirm an SD error is displayed and MotoNav remains responsive.

## 5. GNSS filtering

1. Test outdoors with a stable fix and confirm normal points are recorded.
2. Temporarily obstruct the antenna and confirm unreliable points are skipped.
3. Check the resulting route for stationary clusters and impossible jumps.

## 6. Long recording

1. Record continuously for at least 60 minutes.
2. Confirm the point counter continues increasing and the UI remains responsive.
3. Finish normally and validate the GPX in a mapping application.
