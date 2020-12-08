// yabridge: a Wine VST bridge
// Copyright (C) 2020  Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <optional>

#include <bitsery/ext/pointer.h>
#include <bitsery/ext/std_optional.h>
#include <bitsery/traits/array.h>
#include <pluginterfaces/vst/ivstcomponent.h>

#include "../common.h"
#include "base.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

/**
 * Wraps around `IComponent` for serialization purposes. See `README.md` for
 * more information on how this works. On the Wine plugin host side this is only
 * used for serialization, and on the plugin side have an implementation that
 * can send control messages.
 *
 * We might be able to do some caching here with the buss infos, but since that
 * sounds like a huge potential source of errors we'll just do pure callbacks
 * for everything other than the edit controller's class ID.
 *
 * TODO: I think it's expected that components also implement `IAudioProcessor`
 *       and `IConnectionPoint`.
 */
class YaComponent : public Steinberg::Vst::IComponent {
   public:
    /**
     * These are the arguments for creating a `YaComponentPluginImpl`.
     */
    struct Arguments {
        /**
         * Read arguments from an existing implementation.
         */
        Arguments(Steinberg::IPtr<Steinberg::Vst::IComponent> component,
                  size_t isntance_id);

        /**
         * The unique identifier for this specific instance.
         */
        native_size_t instance_id;

        /**
         * The class ID of this component's corresponding editor controller. You
         * can't use C-style array in `std::optional`s.
         */
        std::optional<ArrayUID> edit_controller_cid;

        template <typename S>
        void serialize(S& s) {
            s.value8b(instance_id);
            s.ext(edit_controller_cid, bitsery::ext::StdOptional{},
                  [](S& s, auto& cid) { s.container1b(cid); });
        }
    };

    /**
     * Message to request the Wine plugin host to instantiate a new IComponent
     * to pass through a call to `IPluginFactory::createInstance(cid,
     * IComponent::iid,
     * ...)`.
     */
    struct Create {
        // TODO: Create a `native_tvalue` wrapper, and then also add them here
        using Response = std::optional<Arguments>;

        ArrayUID cid;

        template <typename S>
        void serialize(S& s) {
            s.container1b(cid);
        }
    };

    /**
     * Instantiate this instance with arguments read from another interface
     * implementation.
     */
    YaComponent(const Arguments&& args);

    /**
     * @remark The plugin side implementation should send a control message to
     *   clean up the instance on the Wine side in its destructor.
     */
    virtual ~YaComponent() = 0;

    DECLARE_FUNKNOWN_METHODS

    // From `IPluginBase`
    virtual tresult PLUGIN_API initialize(FUnknown* context) override = 0;
    virtual tresult PLUGIN_API terminate() override = 0;

    // From `IComponent`
    tresult PLUGIN_API getControllerClassId(Steinberg::TUID classId) override;
    virtual tresult PLUGIN_API
    setIoMode(Steinberg::Vst::IoMode mode) override = 0;
    virtual int32 PLUGIN_API
    getBusCount(Steinberg::Vst::MediaType type,
                Steinberg::Vst::BusDirection dir) override = 0;
    virtual tresult PLUGIN_API
    getBusInfo(Steinberg::Vst::MediaType type,
               Steinberg::Vst::BusDirection dir,
               int32 index,
               Steinberg::Vst::BusInfo& bus /*out*/) override = 0;
    virtual tresult PLUGIN_API
    getRoutingInfo(Steinberg::Vst::RoutingInfo& inInfo,
                   Steinberg::Vst::RoutingInfo& outInfo /*out*/) override = 0;
    virtual tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type,
                                           Steinberg::Vst::BusDirection dir,
                                           int32 index,
                                           TBool state) override = 0;
    virtual tresult PLUGIN_API setActive(TBool state) override = 0;
    virtual tresult PLUGIN_API
    setState(Steinberg::IBStream* state) override = 0;
    virtual tresult PLUGIN_API
    getState(Steinberg::IBStream* state) override = 0;

   private:
    Arguments arguments;

    // TODO: As explained in a few other places, `YaComponent` objects should be
    //       assigned a unique ID for identification
};

template <typename S>
void serialize(S& s, std::optional<YaComponent::Arguments>& args) {
    s.ext(args, bitsery::ext::StdOptional{});
}

#pragma GCC diagnostic pop
