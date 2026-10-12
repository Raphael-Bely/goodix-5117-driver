# libfprint integration

This directory holds everything that belongs to libfprint rather than to the
standalone library in `src/`.

| File | Role |
| --- | --- |
| `goodix5117.c` | the driver: translates libfprint's calls into `src/` calls, on a worker thread |
| `libfprint-custom-matching.patch` | the libfprint changes the driver depends on |

## Why libfprint has to be patched

Image drivers in libfprint are hardwired to NBIS: minutiae extraction, then
`bozorth3`. Measured on this sensor, a 64x80 frame yields **one** minutia
where `bozorth3` wants around twenty, so that path can never work — lowering
the threshold would accept any finger rather than fix anything.

The patch adds two optional entry points to `FpImageDeviceClass`:

```c
GVariant * (*serialize_image) (FpImageDevice *dev, FpImage *image);
gboolean   (*compare_data)    (FpImageDevice *dev, GVariant *probe,
                               GVariant *enrolled);
```

A driver that sets both takes over: minutiae detection is skipped, prints
carry whatever the driver serialises, and the driver decides matches.
libfprint stays unaware of how the comparison works, and every other driver
is untouched.

## Applying it

```
git clone https://gitlab.freedesktop.org/libfprint/libfprint.git
cd libfprint && git apply ../driver/libfprint-custom-matching.patch
cd .. && make sync-driver
```

Then build libfprint into a prefix of its own, as `libfprint-env.sh` expects.
