/*
 * Copyright (c) 2009 Tias Guns
 * Copyright 2007 Peter Hutterer (xinput_ methods from xinput)
 * Copyright (c) 2011 Antoine Hue (invertX/Y)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "calibrator/Evdev.hpp"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <ctype.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 1
#endif
#ifndef EXIT_FAILURE
#define EXIT_FAILURE 0
#endif

// Constructor
CalibratorEvdev::CalibratorEvdev(const char* const device_name0,
                                 const XYinfo& axys0,
                                 XID device_id,
                                 const int thr_misclick,
                                 const int thr_doubleclick,
                                 const OutputType output_type,
                                 const char* geometry)
  : Calibrator(device_name0, axys0, thr_misclick, thr_doubleclick, output_type, geometry)
{
    // init
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        throw WrongCalibratorException("Evdev: Unable to connect to X server");
    }

    // normaly, we already have the device id
    if (device_id == (XID)-1) {
        devInfo = xinput_find_device_info(display, device_name, False);
        if (!devInfo) {
            XCloseDisplay(display);
            throw WrongCalibratorException("Evdev: Unable to find device");
        }
        device_id = devInfo->id;
    }

    dev = XOpenDevice(display, device_id);
    if (!dev) {
        XCloseDisplay(display);
        throw WrongCalibratorException("Evdev: Unable to open device");
    }

#ifndef HAVE_XI_PROP
    throw WrongCalibratorException("Evdev: you need at least libXi 1.2 and inputproto 1.5 for dynamic recalibration of evdev.");
#endif

    // XGetDeviceProperty vars
    Atom            property;
    Atom            act_type;
    int             act_format;
    unsigned long   nitems, bytes_after;
    unsigned char   *data;

    // get "Evdev Axis Calibration" property
    property = xinput_parse_atom(display, "Evdev Axis Calibration");
    if (XGetDeviceProperty(display, dev, property, 0, 1000, False,
                           AnyPropertyType, &act_type, &act_format,
                           &nitems, &bytes_after, &data) != Success)
    {
        XCloseDevice(display, dev);
        XCloseDisplay(display);
        throw WrongCalibratorException("Evdev: \"Evdev Axis Calibration\" property missing, not a (valid) evdev device");
    } else {
        XFree(data);
    }

    printf("Calibrating EVDEV driver for \"%s\" id=%i\n", device_name, (int)device_id);
}

// protected pass-through constructor for subclasses
CalibratorEvdev::CalibratorEvdev(const char* const device_name0,
                                 const XYinfo& axys0,
                                 const int thr_misclick,
                                 const int thr_doubleclick,
                                 const OutputType output_type,
                                 const char* geometry)
  : Calibrator(device_name0, axys0, thr_misclick, thr_doubleclick, output_type, geometry) { }

// Destructor
CalibratorEvdev::~CalibratorEvdev () {
    XCloseDevice(display, dev);
    XCloseDisplay(display);
}

void CalibratorEvdev::detect_axys()
{
    // XGetDeviceProperty vars
    Atom            property;
    Atom            act_type;
    int             act_format;
    unsigned long   nitems, bytes_after;
    unsigned char   *data, *ptr;

    // get "Evdev Axis Calibration" property
    property = xinput_parse_atom(display, "Evdev Axis Calibration");
    if (XGetDeviceProperty(display, dev, property, 0, 1000, False,
                           AnyPropertyType, &act_type, &act_format,
                           &nitems, &bytes_after, &data) == Success)
    {

        if (act_format == 32 && act_type == XA_INTEGER) {
            if (nitems == 0) {
                if (verbose)
                    printf("DEBUG: Evdev Axis Calibration not set, setting to axis valuators to be sure.\n");

                // No axis calibration set, set it to the default one
                // QUIRK: when my machine resumes from a sleep, the calibration
                // property is no longer exported through xinput, but still
                // active not setting the values here would result in a wrong
                // first calibration
                (void) set_calibration(old_axys);
            } else {
                ptr = data;

                old_axys.x.min = *((long*)ptr);
                ptr += sizeof(long);
                old_axys.x.max = *((long*)ptr);
                ptr += sizeof(long);
                old_axys.y.min = *((long*)ptr);
                ptr += sizeof(long);
                old_axys.y.max = *((long*)ptr);
                ptr += sizeof(long);
            }
        }

        XFree(data);
    }

    // get "Evdev Axes Swap" property
    property = xinput_parse_atom(display, "Evdev Axes Swap");
    if (XGetDeviceProperty(display, dev, property, 0, 1000, False,
                           AnyPropertyType, &act_type, &act_format,
                           &nitems, &bytes_after, &data) == Success)
    {
        if (act_format == 8 && act_type == XA_INTEGER && nitems == 1) {
            old_axys.swap_xy = *((char*)data);

            if (verbose)
                printf("DEBUG: Read axes swap value of %i.\n", old_axys.swap_xy);
        }
    }

    // get "Evdev Axes Inversion" property
    property = xinput_parse_atom(display, "Evdev Axis Inversion");
    if (XGetDeviceProperty(display, dev, property, 0, 1000, False,
                AnyPropertyType, &act_type, &act_format,
                &nitems, &bytes_after, &data) == Success) {
        if (act_format == 8 && act_type == XA_INTEGER && nitems == 2) {
            bool invert_x = *((char*)data++);
            bool invert_y = *((char*)data);

            if (verbose)
                printf("DEBUG: Read InvertX=%i, InvertY=%i.\n", invert_x, invert_y);

            if (invert_x)
                std::swap(old_axys.x.min, old_axys.x.max);
            if (invert_y)
                std::swap(old_axys.y.min, old_axys.y.max);
        }
    }

    printf("\tcurrent calibration values (from XInput): min_x=%d, max_x=%d and min_y=%d, max_y=%d\n",
                old_axys.x.min, old_axys.x.max, old_axys.y.min, old_axys.y.max);

    orig_axys = old_axys;
}

// Activate calibrated data and output it
bool CalibratorEvdev::finish_data(const XYinfo new_axys)
{
    bool success = true;

    success &= apply(new_axys);

    printf("\t--> Making the calibration permanent <--\n");
    switch (output_type) {
        case OUTYPE_AUTO:
            // xorg.conf.d or alternatively xinput commands
            if (has_xorgconfd_support()) {
                success &= output_xorgconfd(new_axys);
            } else {
                success &= output_xinput(new_axys);
            }
            break;
        case OUTYPE_XORGCONFD:
            success &= output_xorgconfd(new_axys);
            break;
        case OUTYPE_HAL:
            success &= output_hal(new_axys);
            break;
        case OUTYPE_XINPUT:
            success &= output_xinput(new_axys);
            break;
        default:
            fprintf(stderr, "ERROR: Evdev Calibrator does not support the supplied --output-type\n");
            success = false;
    }

    return success;
}

bool CalibratorEvdev::set_swapxy(const int swap_xy)
{
    printf("\tSwapping X and Y axis...\n");

    // xinput set-int-prop "divername" "Evdev Axes Swap" 8 0
    int arr_cmd[1];
    arr_cmd[0] = swap_xy;

    bool ret = xinput_do_set_int_prop("Evdev Axes Swap", display, 8, 1, arr_cmd);

    if (verbose) {
        if (ret == true)
            printf("DEBUG: Successfully set swapped X and Y axes = %d.\n", swap_xy);
        else
            printf("DEBUG: Failed to set swap X and Y axes.\n");
    }

    return ret;
}

bool CalibratorEvdev::set_invert_xy(const int invert_x, const int invert_y)
{
    printf("\tInverting X and/or Y axis...\n");

    // xinput set-int-prop "divername" "Evdev Axis Inversion" 8 0 0
    int arr_cmd[2];
    arr_cmd[0] = invert_x;
    arr_cmd[1] = invert_y;

    int ret = xinput_do_set_int_prop("Evdev Axis Inversion", display, 8, 2, arr_cmd);

    if (verbose) {
        if (ret == true)
            printf("DEBUG: Successfully set invert axis X=%d, Y=%d.\n", invert_x, invert_y);
        else
            printf("DEBUG: Failed to set axis inversion.\n");
    }

    return ret;
}

bool CalibratorEvdev::set_calibration(const XYinfo new_axys)
{
    printf("\tSetting calibration data: %d, %d, %d, %d\n", new_axys.x.min, new_axys.x.max, new_axys.y.min, new_axys.y.max);

    // xinput set-int-prop 4 223 32 5 500 8 300
    int arr_cmd[4];
    arr_cmd[0] = new_axys.x.min;
    arr_cmd[1] = new_axys.x.max;
    arr_cmd[2] = new_axys.y.min;
    arr_cmd[3] = new_axys.y.max;

    int ret = xinput_do_set_int_prop("Evdev Axis Calibration", display, 32, 4, arr_cmd);

    if (verbose) {
        if (ret == true)
            printf("DEBUG: Successfully applied axis calibration.\n");
        else
            printf("DEBUG: Failed to apply axis calibration.\n");
    }

    return ret;
}

bool CalibratorEvdev::apply(const XYinfo new_axys)
{
    bool success = true;

    printf("\nDoing dynamic recalibration:\n");
    // Evdev Axes Swap
    if (old_axys.swap_xy != new_axys.swap_xy) {
        success &= set_swapxy(new_axys.swap_xy);
    }

    // Evdev Axis Inversion
    set_invert_xy(false, false);

    // Evdev Axis Calibration
    success &= set_calibration(new_axys);

    // close
    XSync(display, False);

    return success;
}

Atom CalibratorEvdev::xinput_parse_atom(Display *display, const char *name)
{
    Bool is_atom = True;
    int i;

    for (i = 0; name[i] != '\0'; i++) {
        if (!isdigit(name[i])) {
            is_atom = False;
            break;
        }
    }

    if (is_atom)
        return atoi(name);
    else
        return XInternAtom(display, name, False);
}

XDeviceInfo* CalibratorEvdev::xinput_find_device_info(
Display *display, const char *name, Bool only_extended)
{
    XDeviceInfo	*devices;
    XDeviceInfo *found = NULL;
    int		loop;
    int		num_devices;
    int		len = strlen(name);
    Bool	is_id = True;
    XID		id = (XID)-1;

    for (loop=0; loop<len; loop++) {
        if (!isdigit(name[loop])) {
            is_id = False;
            break;
        }
    }

    if (is_id) {
        id = atoi(name);
    }

    devices = XListInputDevices(display, &num_devices);

    for (loop=0; loop<num_devices; loop++) {
        if ((!only_extended || (devices[loop].use >= IsXExtensionDevice)) &&
            ((!is_id && strcmp(devices[loop].name, name) == 0) ||
             (is_id && devices[loop].id == id))) {
            if (found) {
                fprintf(stderr,
                        "Warning: There are multiple devices named \"%s\".\n"
                        "To ensure the correct one is selected, please use "
                        "the device ID instead.\n\n", name);
                return NULL;
            } else {
                found = &devices[loop];
            }
        }
    }

    return found;
}

// Set Integer property on  X
bool CalibratorEvdev::xinput_do_set_int_prop( const char * name,
                                         Display *display,
                                         int format,
                                         int argc,
                                         const int *argv )
{
#ifndef HAVE_XI_PROP
    return false;
#else

    Atom          prop;
    Atom          old_type;
    int           i;
    int           old_format;
    unsigned long act_nitems, bytes_after;

    union {
        unsigned char *c;
        short *s;
        long *l;
        Atom *a;
    } data;

    if (argc < 1)
    {
        fprintf(stderr, "Wrong usage of xinput_do_set_prop, need at least 1 arguments\n");
        return false;
    }

    prop = xinput_parse_atom(display, name);

    if (prop == None) {
        fprintf(stderr, "invalid property %s\n", name);
        return false;
    }

    if ( format == 0) {
        if (XGetDeviceProperty(display, dev, prop, 0, 0, False, AnyPropertyType,
                               &old_type, &old_format, &act_nitems,
                               &bytes_after, &data.c) != Success) {
            fprintf(stderr, "failed to get property type and format for %s\n",
                    name);
            return false;
        } else {
            format = old_format;
        }

        XFree(data.c);
    }

    data.c = (unsigned char*)calloc(argc, sizeof(long));

    for (i = 0; i < argc; i++) {
      switch (format) {
        case 8:
            data.c[i] = argv[i];
        case 16:
            data.s[i] = argv[i];
            break;
        case 32:
            data.l[i] = argv[i];
            break;

        default:
            fprintf(stderr, "unexpected size for property %s\n", name);
            return false;
      }
    }

    XChangeDeviceProperty(display, dev, prop, XA_INTEGER, format, PropModeReplace,
                      data.c, argc);
    free(data.c);
    return true;
#endif // HAVE_XI_PROP

}

bool CalibratorEvdev::output_xorgconfd(const XYinfo new_axys)
{
    const char* sysfs_name = get_sysfs_name();
    bool not_sysfs_name = (sysfs_name == NULL);
    if (not_sysfs_name)
        sysfs_name = "!!Name_Of_TouchScreen!!";

    // xorg.conf.d snippet
    printf("  copy the snippet below into '/etc/X11/xorg.conf.d/99-calibration.conf'\n");
    printf("Section \"InputClass\"\n");
    printf("	Identifier	\"calibration\"\n");
    printf("	MatchProduct	\"%s\"\n", sysfs_name);
    printf("	Option	\"Calibration\"	\"%d %d %d %d\"\n",
                new_axys.x.min, new_axys.x.max, new_axys.y.min, new_axys.y.max);
    printf("	Option	\"SwapAxes\"	\"%d\"\n", new_axys.swap_xy);
    printf("EndSection\n");

    if (not_sysfs_name)
        printf("\nChange '%s' to your device's name in the snippet above.\n", sysfs_name);

    return true;
}

bool CalibratorEvdev::output_hal(const XYinfo new_axys)
{
    const char* sysfs_name = get_sysfs_name();
    bool not_sysfs_name = (sysfs_name == NULL);
    if (not_sysfs_name)
        sysfs_name = "!!Name_Of_TouchScreen!!";

    // HAL policy output
    printf("  copy the policy below into '/etc/hal/fdi/policy/touchscreen.fdi'\n\
<match key=\"info.product\" contains=\"%s\">\n\
  <merge key=\"input.x11_options.calibration\" type=\"string\">%d %d %d %d</merge>\n"
     , sysfs_name, new_axys.x.min, new_axys.x.max, new_axys.y.min, new_axys.y.max);
    printf("  <merge key=\"input.x11_options.swapaxes\" type=\"string\">%d</merge>\n", new_axys.swap_xy);
    printf("</match>\n");

    if (not_sysfs_name)
        printf("\nChange '%s' to your device's name in the config above.\n", sysfs_name);

    return true;
}

bool CalibratorEvdev::output_xinput(const XYinfo new_axys)
{
    // create startup script
    printf("  Install the 'xinput' tool and copy the command(s) below in a script that starts with your X session\n");
    printf("    xinput set-int-prop \"%s\" \"Evdev Axis Calibration\" 32 %d %d %d %d\n", device_name, new_axys.x.min, new_axys.x.max, new_axys.y.min, new_axys.y.max);
    printf("    xinput set-int-prop \"%s\" \"Evdev Axes Swap\" 8 %d\n", device_name, new_axys.swap_xy);

    return true;
}
