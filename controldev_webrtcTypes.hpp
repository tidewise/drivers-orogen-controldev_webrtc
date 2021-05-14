#ifndef controldev_webrtc_TYPES_HPP
#define controldev_webrtc_TYPES_HPP

#include <string>
#include <base/Time.hpp>

namespace controldev_webrtc
{
    struct Mapping
    {
        enum Type {
            Axis = 0,
            Button = 1
        };

        /**
         * Enum to represent an axis or a button.
         */
        Type type;

        int index;
    };

    struct ButtonMapping: public Mapping
    {
        double threshold = 0.5;
    };

    struct Statistics
    {
        int errors = 0;
        int received = 0;
        base::Time time;
    };
} // end namespace controldev_webrtc

#endif
