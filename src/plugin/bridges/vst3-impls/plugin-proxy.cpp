// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
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

#include "plugin-proxy.h"

#include "plug-view-proxy.h"

Vst3PluginProxyImpl::Vst3PluginProxyImpl(Vst3PluginBridge& bridge,
                                         Vst3PluginProxy::ConstructArgs&& args)
    : Vst3PluginProxy(std::move(args)), bridge(bridge) {
    bridge.register_plugin_proxy(*this);
}

Vst3PluginProxyImpl::~Vst3PluginProxyImpl() {
    bridge.send_message(
        Vst3PluginProxy::Destruct{.instance_id = instance_id()});
    bridge.unregister_plugin_proxy(*this);
}

tresult PLUGIN_API
Vst3PluginProxyImpl::queryInterface(const Steinberg::TUID _iid, void** obj) {
    const tresult result = Vst3PluginProxy::queryInterface(_iid, obj);
    bridge.logger.log_query_interface("In FUnknown::queryInterface()", result,
                                      Steinberg::FUID::fromTUID(_iid));

    return result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs,
    int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs,
    int32 numOuts) {
    // NOTE: Ardour passes a null pointer when `numIns` or `numOuts` is 0, so we
    //       need to work around that
    return bridge.send_audio_processor_message(
        YaAudioProcessor::SetBusArrangements{
            .instance_id = instance_id(),
            .inputs =
                (inputs ? std::vector<Steinberg::Vst::SpeakerArrangement>(
                              inputs, &inputs[numIns])
                        : std::vector<Steinberg::Vst::SpeakerArrangement>()),
            .num_ins = numIns,
            .outputs =
                (outputs ? std::vector<Steinberg::Vst::SpeakerArrangement>(
                               outputs, &outputs[numOuts])
                         : std::vector<Steinberg::Vst::SpeakerArrangement>()),
            .num_outs = numOuts,
        });
}

tresult PLUGIN_API Vst3PluginProxyImpl::getBusArrangement(
    Steinberg::Vst::BusDirection dir,
    int32 index,
    Steinberg::Vst::SpeakerArrangement& arr) {
    const GetBusArrangementResponse response =
        bridge.send_audio_processor_message(
            YaAudioProcessor::GetBusArrangement{.instance_id = instance_id(),
                                                .dir = dir,
                                                .index = index,
                                                .arr = arr});

    arr = response.updated_arr;

    return response.result;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::canProcessSampleSize(int32 symbolicSampleSize) {
    return bridge.send_audio_processor_message(
        YaAudioProcessor::CanProcessSampleSize{
            .instance_id = instance_id(),
            .symbolic_sample_size = symbolicSampleSize});
}

uint32 PLUGIN_API Vst3PluginProxyImpl::getLatencySamples() {
    return bridge.send_audio_processor_message(
        YaAudioProcessor::GetLatencySamples{.instance_id = instance_id()});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setupProcessing(Steinberg::Vst::ProcessSetup& setup) {
    return bridge.send_audio_processor_message(
        YaAudioProcessor::SetupProcessing{.instance_id = instance_id(),
                                          .setup = setup});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setProcessing(TBool state) {
    return bridge.send_audio_processor_message(YaAudioProcessor::SetProcessing{
        .instance_id = instance_id(), .state = state});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::process(Steinberg::Vst::ProcessData& data) {
    // TODO: Check whether reusing a `YaProcessData` object make a difference in
    //       terms of performance
    ProcessResponse response = bridge.send_audio_processor_message(
        YaAudioProcessor::Process{.instance_id = instance_id(), .data = data});

    response.output_data.write_back_outputs(data);

    return response.result;
}

uint32 PLUGIN_API Vst3PluginProxyImpl::getTailSamples() {
    return bridge.send_audio_processor_message(
        YaAudioProcessor::GetTailSamples{.instance_id = instance_id()});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::getControllerClassId(Steinberg::TUID classId) {
    const GetControllerClassIdResponse response =
        bridge.send_audio_processor_message(
            YaComponent::GetControllerClassId{.instance_id = instance_id()});

    std::copy(response.editor_cid.begin(), response.editor_cid.end(), classId);

    return response.result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::setIoMode(Steinberg::Vst::IoMode mode) {
    return bridge.send_audio_processor_message(
        YaComponent::SetIoMode{.instance_id = instance_id(), .mode = mode});
}

int32 PLUGIN_API
Vst3PluginProxyImpl::getBusCount(Steinberg::Vst::MediaType type,
                                 Steinberg::Vst::BusDirection dir) {
    return bridge.send_audio_processor_message(YaComponent::GetBusCount{
        .instance_id = instance_id(), .type = type, .dir = dir});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::getBusInfo(Steinberg::Vst::MediaType type,
                                Steinberg::Vst::BusDirection dir,
                                int32 index,
                                Steinberg::Vst::BusInfo& bus /*out*/) {
    const GetBusInfoResponse response = bridge.send_audio_processor_message(
        YaComponent::GetBusInfo{.instance_id = instance_id(),
                                .type = type,
                                .dir = dir,
                                .index = index,
                                .bus = bus});

    bus = response.updated_bus;
    return response.result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::getRoutingInfo(
    Steinberg::Vst::RoutingInfo& inInfo,
    Steinberg::Vst::RoutingInfo& outInfo /*out*/) {
    const GetRoutingInfoResponse response = bridge.send_audio_processor_message(
        YaComponent::GetRoutingInfo{.instance_id = instance_id(),
                                    .in_info = inInfo,
                                    .out_info = outInfo});

    inInfo = response.updated_in_info;
    outInfo = response.updated_out_info;
    return response.result;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::activateBus(Steinberg::Vst::MediaType type,
                                 Steinberg::Vst::BusDirection dir,
                                 int32 index,
                                 TBool state) {
    return bridge.send_audio_processor_message(
        YaComponent::ActivateBus{.instance_id = instance_id(),
                                 .type = type,
                                 .dir = dir,
                                 .index = index,
                                 .state = state});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setActive(TBool state) {
    return bridge.send_audio_processor_message(
        YaComponent::SetActive{.instance_id = instance_id(), .state = state});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setState(Steinberg::IBStream* state) {
    // Since both interfaces contain this function, this is used for both
    // `IComponent::setState()` as well as `IEditController::setState()`
    return bridge.send_message(Vst3PluginProxy::SetState{
        .instance_id = instance_id(), .state = state});
}

tresult PLUGIN_API Vst3PluginProxyImpl::getState(Steinberg::IBStream* state) {
    // Since both interfaces contain this function, this is used for both
    // `IComponent::getState()` as well as `IEditController::getState()`
    const GetStateResponse response = bridge.send_message(
        Vst3PluginProxy::GetState{.instance_id = instance_id()});

    assert(response.updated_state.write_back(state) == Steinberg::kResultOk);

    return response.result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::connect(IConnectionPoint* other) {
    // When the host is trying to connect two plugin proxy objects, we can just
    // identify the other object by its instance IDs and then connect the
    // objects in the Wine plugin host directly. Otherwise we'll have to set up
    // a proxy for the host's connection proxy so the messages can be routed
    // through that.
    if (auto other_proxy = dynamic_cast<Vst3PluginProxy*>(other)) {
        return bridge.send_message(YaConnectionPoint::Connect{
            .instance_id = instance_id(), .other = other_proxy->instance_id()});
    } else {
        connection_point_proxy = other;

        return bridge.send_message(YaConnectionPoint::Connect{
            .instance_id = instance_id(),
            .other =
                Vst3ConnectionPointProxy::ConstructArgs(other, instance_id())});
    }
}

tresult PLUGIN_API Vst3PluginProxyImpl::disconnect(IConnectionPoint* other) {
    // See `Vst3PluginProxyImpl::connect()`
    if (auto other_proxy = dynamic_cast<Vst3PluginProxy*>(other)) {
        return bridge.send_message(YaConnectionPoint::Disconnect{
            .instance_id = instance_id(),
            .other_instance_id = other_proxy->instance_id()});
    } else {
        const tresult result = bridge.send_message(
            YaConnectionPoint::Disconnect{.instance_id = instance_id(),
                                          .other_instance_id = std::nullopt});
        connection_point_proxy.reset();

        return result;
    }
}

tresult PLUGIN_API
Vst3PluginProxyImpl::notify(Steinberg::Vst::IMessage* message) {
    // Since there is no way to enumerate over all values in an
    // `IAttributeList`, we can only support relaying messages that were sent by
    // our own objects. Additionally, the `IMessage*` we end up passing to the
    // plugin needs to have the same lifetime as the original object, because
    // some plugins are being a bit naughty. That's why we pass around a pointer
    // to the original message object.
    // All of this is only needed to support hosts that place a connection proxy
    // between two objects instead of connecting them directly.  If the objects
    // are connected directly we also connected them directly on the Wine side,
    // so we don't have to do any additional when those objects pass through
    // messages.
    if (auto message_ptr = dynamic_cast<YaMessagePtr*>(message)) {
        return bridge.send_message(YaConnectionPoint::Notify{
            .instance_id = instance_id(), .message_ptr = *message_ptr});
    } else {
        bridge.logger.log(
            "WARNING: Unknown message type passed to "
            "'IConnectionPoint::notify()', ignoring");
        return Steinberg::kNotImplemented;
    }
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setComponentState(Steinberg::IBStream* state) {
    return bridge.send_message(YaEditController::SetComponentState{
        .instance_id = instance_id(), .state = state});
}

int32 PLUGIN_API Vst3PluginProxyImpl::getParameterCount() {
    return bridge.send_message(
        YaEditController::GetParameterCount{.instance_id = instance_id()});
}

tresult PLUGIN_API Vst3PluginProxyImpl::getParameterInfo(
    int32 paramIndex,
    Steinberg::Vst::ParameterInfo& info /*out*/) {
    const GetParameterInfoResponse response = bridge.send_message(
        YaEditController::GetParameterInfo{.instance_id = instance_id(),
                                           .param_index = paramIndex,
                                           .info = info});

    info = response.updated_info;

    return response.result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::getParamStringByValue(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized /*in*/,
    Steinberg::Vst::String128 string /*out*/) {
    const GetParamStringByValueResponse response =
        bridge.send_message(YaEditController::GetParamStringByValue{
            .instance_id = instance_id(),
            .id = id,
            .value_normalized = valueNormalized});

    std::copy(response.string.begin(), response.string.end(), string);
    string[response.string.size()] = 0;

    return response.result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::getParamValueByString(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::TChar* string /*in*/,
    Steinberg::Vst::ParamValue& valueNormalized /*out*/) {
    const GetParamValueByStringResponse response =
        bridge.send_message(YaEditController::GetParamValueByString{
            .instance_id = instance_id(), .id = id, .string = string});

    valueNormalized = response.value_normalized;

    return response.result;
}

Steinberg::Vst::ParamValue PLUGIN_API
Vst3PluginProxyImpl::normalizedParamToPlain(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized) {
    return bridge.send_message(YaEditController::NormalizedParamToPlain{
        .instance_id = instance_id(),
        .id = id,
        .value_normalized = valueNormalized});
}

Steinberg::Vst::ParamValue PLUGIN_API
Vst3PluginProxyImpl::plainParamToNormalized(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue plainValue) {
    return bridge.send_message(YaEditController::PlainParamToNormalized{
        .instance_id = instance_id(), .id = id, .plain_value = plainValue});
}

Steinberg::Vst::ParamValue PLUGIN_API
Vst3PluginProxyImpl::getParamNormalized(Steinberg::Vst::ParamID id) {
    return bridge.send_message(YaEditController::GetParamNormalized{
        .instance_id = instance_id(), .id = id});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setParamNormalized(Steinberg::Vst::ParamID id,
                                        Steinberg::Vst::ParamValue value) {
    return bridge.send_message(YaEditController::SetParamNormalized{
        .instance_id = instance_id(), .id = id, .value = value});
}

tresult PLUGIN_API Vst3PluginProxyImpl::setComponentHandler(
    Steinberg::Vst::IComponentHandler* handler) {
    if (handler) {
        // We'll store the pointer for when the plugin later makes a callback to
        // this component handler
        component_handler = handler;

        // Automatically converted smart pointers for when the plugin performs a
        // callback later
        unit_handler = component_handler;

        return bridge.send_message(YaEditController::SetComponentHandler{
            .instance_id = instance_id(),
            .component_handler_proxy_args =
                Vst3ComponentHandlerProxy::ConstructArgs(component_handler,
                                                         instance_id())});
    } else {
        bridge.logger.log(
            "WARNING: Null pointer passed to "
            "'IEditController::setComponentHandler()'");
        return Steinberg::kInvalidArgument;
    }
}

Steinberg::IPlugView* PLUGIN_API
Vst3PluginProxyImpl::createView(Steinberg::FIDString name) {
    CreateViewResponse response =
        bridge.send_message(YaEditController::CreateView{
            .instance_id = instance_id(), .name = name});

    if (response.plug_view_args) {
        // The host should manage this. Returning raw pointers feels scary.
        auto plug_view_proxy = new Vst3PlugViewProxyImpl(
            bridge, std::move(*response.plug_view_args));

        // We also need to store an (unmanaged, since we don't want to affect
        // the reference counting) pointer to this to be able to handle calls to
        // `IPlugFrame::resizeView()` in the future
        last_created_plug_view = plug_view_proxy;

        return plug_view_proxy;
    } else {
        return nullptr;
    }
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setKnobMode(Steinberg::Vst::KnobMode mode) {
    return bridge.send_message(YaEditController2::SetKnobMode{
        .instance_id = instance_id(), .mode = mode});
}

tresult PLUGIN_API Vst3PluginProxyImpl::openHelp(TBool onlyCheck) {
    return bridge.send_message(YaEditController2::OpenHelp{
        .instance_id = instance_id(), .only_check = onlyCheck});
}

tresult PLUGIN_API Vst3PluginProxyImpl::openAboutBox(TBool onlyCheck) {
    return bridge.send_message(YaEditController2::OpenAboutBox{
        .instance_id = instance_id(), .only_check = onlyCheck});
}

tresult PLUGIN_API Vst3PluginProxyImpl::initialize(FUnknown* context) {
    if (context) {
        // We will create a proxy object that that supports all the same
        // interfaces as `context`, and then we'll store `context` in this
        // object. We can then use it to handle callbacks made by the Windows
        // VST3 plugin to this context.
        host_context = context;

        // Automatically converted smart pointers for when the plugin performs a
        // callback later
        host_application = host_context;

        return bridge.send_message(YaPluginBase::Initialize{
            .instance_id = instance_id(),
            .host_context_args = Vst3HostContextProxy::ConstructArgs(
                host_context, instance_id())});
    } else {
        bridge.logger.log(
            "WARNING: Null pointer passed to 'IPluginBase::initialize()'");
        return Steinberg::kInvalidArgument;
    }
}

tresult PLUGIN_API Vst3PluginProxyImpl::terminate() {
    return bridge.send_message(
        YaPluginBase::Terminate{.instance_id = instance_id()});
}

tresult PLUGIN_API Vst3PluginProxyImpl::programDataSupported(
    Steinberg::Vst::ProgramListID listId) {
    return bridge.send_message(YaProgramListData::ProgramDataSupported{
        .instance_id = instance_id(), .list_id = listId});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::getProgramData(Steinberg::Vst::ProgramListID listId,
                                    int32 programIndex,
                                    Steinberg::IBStream* data) {
    const GetProgramDataResponse response = bridge.send_message(
        YaProgramListData::GetProgramData{.instance_id = instance_id(),
                                          .list_id = listId,
                                          .program_index = programIndex});

    assert(response.data.write_back(data) == Steinberg::kResultOk);

    return response.result;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setProgramData(Steinberg::Vst::ProgramListID listId,
                                    int32 programIndex,
                                    Steinberg::IBStream* data) {
    return bridge.send_message(
        YaProgramListData::SetProgramData{.instance_id = instance_id(),
                                          .list_id = listId,
                                          .program_index = programIndex,
                                          .data = data});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::unitDataSupported(Steinberg::Vst::UnitID unitId) {
    return bridge.send_message(YaUnitData::UnitDataSupported{
        .instance_id = instance_id(), .unit_id = unitId});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::getUnitData(Steinberg::Vst::UnitID unitId,
                                 Steinberg::IBStream* data) {
    const GetUnitDataResponse response =
        bridge.send_message(YaUnitData::GetUnitData{
            .instance_id = instance_id(), .unit_id = unitId});

    assert(response.data.write_back(data) == Steinberg::kResultOk);

    return response.result;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setUnitData(Steinberg::Vst::UnitID unitId,
                                 Steinberg::IBStream* data) {
    return bridge.send_message(YaUnitData::SetUnitData{
        .instance_id = instance_id(), .unit_id = unitId, .data = data});
}

int32 PLUGIN_API Vst3PluginProxyImpl::getUnitCount() {
    return bridge.send_message(
        YaUnitInfo::GetUnitCount{.instance_id = instance_id()});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::getUnitInfo(int32 unitIndex,
                                 Steinberg::Vst::UnitInfo& info /*out*/) {
    const GetUnitInfoResponse response =
        bridge.send_message(YaUnitInfo::GetUnitInfo{
            .instance_id = instance_id(), .unit_index = unitIndex});

    info = response.info;

    return response.result;
}

int32 PLUGIN_API Vst3PluginProxyImpl::getProgramListCount() {
    return bridge.send_message(
        YaUnitInfo::GetProgramListCount{.instance_id = instance_id()});
}

tresult PLUGIN_API Vst3PluginProxyImpl::getProgramListInfo(
    int32 listIndex,
    Steinberg::Vst::ProgramListInfo& info /*out*/) {
    const GetProgramListInfoResponse response =
        bridge.send_message(YaUnitInfo::GetProgramListInfo{
            .instance_id = instance_id(), .list_index = listIndex});

    info = response.info;

    return response.result;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::getProgramName(Steinberg::Vst::ProgramListID listId,
                                    int32 programIndex,
                                    Steinberg::Vst::String128 name /*out*/) {
    const GetProgramNameResponse response = bridge.send_message(
        YaUnitInfo::GetProgramName{.instance_id = instance_id(),
                                   .list_id = listId,
                                   .program_index = programIndex});

    std::copy(response.name.begin(), response.name.end(), name);
    name[response.name.size()] = 0;

    return response.result;
}

tresult PLUGIN_API Vst3PluginProxyImpl::getProgramInfo(
    Steinberg::Vst::ProgramListID listId,
    int32 programIndex,
    Steinberg::Vst::CString attributeId /*in*/,
    Steinberg::Vst::String128 attributeValue /*out*/) {
    assert(attributeId);

    const GetProgramInfoResponse response = bridge.send_message(
        YaUnitInfo::GetProgramInfo{.instance_id = instance_id(),
                                   .list_id = listId,
                                   .program_index = programIndex,
                                   .attribute_id = attributeId});

    std::copy(response.attribute_value.begin(), response.attribute_value.end(),
              attributeValue);
    attributeValue[response.attribute_value.size()] = 0;

    return response.result;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::hasProgramPitchNames(Steinberg::Vst::ProgramListID listId,
                                          int32 programIndex) {
    return bridge.send_message(
        YaUnitInfo::HasProgramPitchNames{.instance_id = instance_id(),
                                         .list_id = listId,
                                         .program_index = programIndex});
}

tresult PLUGIN_API Vst3PluginProxyImpl::getProgramPitchName(
    Steinberg::Vst::ProgramListID listId,
    int32 programIndex,
    int16 midiPitch,
    Steinberg::Vst::String128 name /*out*/) {
    const GetProgramPitchNameResponse response = bridge.send_message(
        YaUnitInfo::GetProgramPitchName{.instance_id = instance_id(),
                                        .list_id = listId,
                                        .program_index = programIndex,
                                        .midi_pitch = midiPitch});

    std::copy(response.name.begin(), response.name.end(), name);
    name[response.name.size()] = 0;

    return response.result;
}

Steinberg::Vst::UnitID PLUGIN_API Vst3PluginProxyImpl::getSelectedUnit() {
    return bridge.send_message(
        YaUnitInfo::GetSelectedUnit{.instance_id = instance_id()});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::selectUnit(Steinberg::Vst::UnitID unitId) {
    return bridge.send_message(YaUnitInfo::SelectUnit{
        .instance_id = instance_id(), .unit_id = unitId});
}

tresult PLUGIN_API
Vst3PluginProxyImpl::getUnitByBus(Steinberg::Vst::MediaType type,
                                  Steinberg::Vst::BusDirection dir,
                                  int32 busIndex,
                                  int32 channel,
                                  Steinberg::Vst::UnitID& unitId /*out*/) {
    const GetUnitByBusResponse response = bridge.send_message(
        YaUnitInfo::GetUnitByBus{.instance_id = instance_id(),
                                 .type = type,
                                 .dir = dir,
                                 .bus_index = busIndex,
                                 .channel = channel});

    unitId = response.unit_id;

    return response.result;
}

tresult PLUGIN_API
Vst3PluginProxyImpl::setUnitProgramData(int32 listOrUnitId,
                                        int32 programIndex,
                                        Steinberg::IBStream* data) {
    return bridge.send_message(
        YaUnitInfo::SetUnitProgramData{.instance_id = instance_id(),
                                       .list_or_unit_id = listOrUnitId,
                                       .program_index = programIndex,
                                       .data = data});
}