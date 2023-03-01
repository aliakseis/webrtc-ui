#include "cameraman.h"

#include <gst/gst.h>

#include <QObject>

namespace {

gchar* get_launch_line(GstDevice * device)
{
    static const char *const ignored_propnames[] =
    { "name", "parent", "direction", "template", "caps", nullptr };

    auto element = gst_device_create_element(device, nullptr);

    if (!element) {
        return nullptr;
    }

    auto factory = gst_element_get_factory(element);
    if (!factory) {
        gst_object_unref(element);
        return nullptr;
    }

    if (!gst_plugin_feature_get_name(factory)) {
        gst_object_unref(element);
        return nullptr;
    }

    auto launch_line = g_string_new(gst_plugin_feature_get_name(factory));

    auto pureelement = gst_element_factory_create(factory, nullptr);

    /* get paramspecs and show non-default properties */
    guint number_of_properties;
    auto properties =
        g_object_class_list_properties(G_OBJECT_GET_CLASS(element),
            &number_of_properties);
    if (properties) {
        GValue value = G_VALUE_INIT;
        GValue pvalue = G_VALUE_INIT;

        for (int i = 0; i < number_of_properties; i++) {
            bool ignore = false;
            auto property = properties[i];

            /* skip some properties */
            if ((property->flags & G_PARAM_READWRITE) != G_PARAM_READWRITE) {
                continue;
            }

            for (int j = 0; ignored_propnames[j]; j++) {
                if (!g_strcmp0(ignored_propnames[j], property->name)) {
                    ignore = true;
                    break;
                }
            }

            if (ignore) {
                continue;
            }

            /* Can't use _param_value_defaults () because sub-classes modify the
                * values already.
                */

            g_value_init(&value, property->value_type);
            g_value_init(&pvalue, property->value_type);
            g_object_get_property(G_OBJECT(element), property->name, &value);
            g_object_get_property(G_OBJECT(pureelement), property->name, &pvalue);
            if (gst_value_compare(&value, &pvalue) != GST_VALUE_EQUAL) {
                gchar *valuestr = gst_value_serialize(&value);

                if (!valuestr) {
                    GST_WARNING("Could not serialize property %s:%s",
                        GST_OBJECT_NAME(element), property->name);
                    g_free(valuestr);
                    goto next;
                }

                g_string_append_printf(launch_line, " %s=%s",
                    property->name, valuestr);
                g_free(valuestr);

            }

        next:
            g_value_unset(&value);
            g_value_unset(&pvalue);
        }
        g_free(properties);
    }

    gst_object_unref(element);
    gst_object_unref(pureelement);

    return g_string_free(launch_line, FALSE);
}


void unescape_value_string(gchar * s)
{
    if (*s == 0) {
        return;
    }

    if (*s != '"') {
        return;
    }

    /* Find the closing quotes */
    gchar* w = s;
    s++;

    while (*s != '"') {
        if (*s == 0) {
            break;
        }
        if (*s == '\\') {
            s++;
            if (*s == 0) {
                break;
            }
        }
        *w = *s;
        w++;
        s++;
    }

    *w = 0;
}


void print_device(GstDevice * device, std::vector<CameraDesc>& result)
{
    CameraDesc desc;

    auto str = get_launch_line(device);
    desc.launchLine = str;
    g_free(str);

    bool has_id = false;

    if (auto props = gst_device_get_properties(device))
    {
        if (const char * path = gst_structure_get_string(props, "device.path"))
        {
            desc.id = path;
            has_id = true;
        }
        gst_structure_free(props);
    }
    if (has_id) {}
    else if (auto element = gst_device_create_element(device, nullptr))
    {
        GValue value = G_VALUE_INIT;
        g_object_get_property(G_OBJECT(element), "device-path", &value);
        if (gchar *valuestr = gst_value_serialize(&value))
        {
            unescape_value_string(valuestr);
            desc.id = valuestr;
            g_free(valuestr);
            has_id = true;
        }
        g_value_unset(&value);
        gst_object_unref(element);
    }

    auto name = gst_device_get_display_name(device);
    desc.description = name;
    if (!has_id)
    {
        desc.id = name;
    }
    g_free(name);


    if (auto caps = gst_device_get_caps(device))
    {
        const auto size = gst_caps_get_size(caps);

        for (int idx = 0; idx < size; ++idx) {
            GstStructure *s = gst_caps_get_structure(caps, idx);
            CameraMode mode{};
            gst_structure_get_int(s, "width", &mode.w);
            gst_structure_get_int(s, "height", &mode.h);
            if (const char* format = gst_structure_get_string(s, "format"))
            {
                mode.format = format;
            }
            if (auto framerates = gst_structure_get_value(s, "framerate")) {
                if (GST_VALUE_HOLDS_FRACTION(framerates))
                {
                    mode.den = gst_value_get_fraction_numerator(framerates);
                    mode.num = gst_value_get_fraction_denominator(framerates);

                    desc.modes.push_back(mode);
                }
                else if (GST_VALUE_HOLDS_LIST(framerates))
                {
                    const auto num_framerates = gst_value_list_get_size(framerates);
                    for (int i = 0; i < num_framerates; ++i)
                    {
                        const GValue *value;
                        value = gst_value_list_get_value(framerates, i);
                        mode.den = gst_value_get_fraction_numerator(value);
                        mode.num = gst_value_get_fraction_denominator(value);

                        desc.modes.push_back(mode);
                    }
                }
                else if (GST_VALUE_HOLDS_FRACTION_RANGE(framerates))
                {
                    const auto fraction_range_min = gst_value_get_fraction_range_min(framerates);
                    const auto numerator_min = gst_value_get_fraction_numerator(fraction_range_min);
                    const auto denominator_min = gst_value_get_fraction_denominator(fraction_range_min);

                    const auto fraction_range_max = gst_value_get_fraction_range_max(framerates);
                    const auto numerator_max = gst_value_get_fraction_numerator(fraction_range_max);
                    const auto denominator_max = gst_value_get_fraction_denominator(fraction_range_max);

                    for (int i = numerator_max; i >= numerator_min; --i)
                    {
                        for (int j = denominator_min; j <= denominator_max; ++j)
                        {
                            mode.den = i;
                            mode.num = j;

                            desc.modes.push_back(mode);
                        }
                    }
                }
            }
        }
        gst_caps_unref(caps);
    }

    result.push_back(std::move(desc));
}

// https://github.com/huskyroboticsteam/Orpheus/blob/084ccacf19f520836f15db4abb37f678d3f20993/Rover/GStreamer/device-scanner.cpp
GstDeviceMonitor *device_monitor()
{
    // starts the monitor for the devices 
    auto monitor = gst_device_monitor_new();

    // adds a filter to scan for only video devices
    auto caps = gst_caps_new_empty_simple("video/x-raw");
    gst_device_monitor_add_filter(monitor, "Video/Source", caps);
    gst_caps_unref(caps);

    return monitor;
}

} // namespace

std::vector<CameraDesc> getCameraDescriptions()
{
    std::vector<CameraDesc> result;

    // create the monitor
    auto monitor = device_monitor();
    auto dev = gst_device_monitor_get_devices(monitor);

    // loop for the lists
    for (GList *cur = g_list_first(dev); cur != nullptr; cur = g_list_next(cur))
    {
        auto * device = static_cast<GstDevice *>(cur->data);
        print_device(device, result);
    }
    gst_object_unref(monitor);

    return result;
}


double CameraMode::fps() const
{
    return static_cast<double>(den) / num;
}

QString CameraMode::getDescr() const
{
    return QObject::tr("%1 %2 x %3 @ %4 FPS [ %5 / %6 sec ]").arg(format).arg(w).arg(h).arg(fps()).arg(num).arg(den);
}
