/*************************************************************************/
/*  openxr_api.cpp                                                       */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "openxr_api.h"
#include "openxr_util.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/os/memory.h"
#include "core/version.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_settings.h"
#endif

#ifdef ANDROID_ENABLED
#include "extensions/openxr_android_extension.h"
#endif

#ifdef VULKAN_ENABLED
#include "extensions/openxr_vulkan_extension.h"
#endif

#include "extensions/openxr_htc_vive_tracker_extension.h"

#include "modules/openxr/openxr_interface.h"

OpenXRAPI *OpenXRAPI::singleton = nullptr;

bool OpenXRAPI::openxr_is_enabled(bool p_check_run_in_editor) {
	// @TODO we need an overrule switch so we can force enable openxr, i.e run "godot --openxr_enabled"

	if (Engine::get_singleton()->is_editor_hint() && p_check_run_in_editor) {
#ifdef TOOLS_ENABLED
		// Disabled for now, using XR inside of the editor we'll be working on during the coming months.
		return false;

		// bool enabled = GLOBAL_GET("xr/openxr/in_editor"); // EDITOR_GET("xr/openxr/in_editor");
		// return enabled;
#else
		// we should never get here, editor hint won't be true if the editor isn't compiled in.
		return false;
#endif
	} else {
		bool enabled = GLOBAL_GET("xr/openxr/enabled");
		return enabled;
	}
}

OpenXRAPI *OpenXRAPI::get_singleton() {
	return singleton;
}

String OpenXRAPI::get_default_action_map_resource_name() {
	String name = GLOBAL_GET("xr/openxr/default_action_map");

	return name;
}

String OpenXRAPI::get_error_string(XrResult result) {
	if (XR_SUCCEEDED(result)) {
		return String("Succeeded");
	}

	if (instance == XR_NULL_HANDLE) {
		Array args;
		args.push_back(Variant(result));
		return String("Error code {0}").format(args);
	}

	char resultString[XR_MAX_RESULT_STRING_SIZE];
	xrResultToString(instance, result, resultString);

	return String(resultString);
}

String OpenXRAPI::get_swapchain_format_name(int64_t p_swapchain_format) const {
	// This is rendering engine dependent...
	if (graphics_extension) {
		return graphics_extension->get_swapchain_format_name(p_swapchain_format);
	}

	return String("Swapchain format ") + String::num_int64(int64_t(p_swapchain_format));
}

bool OpenXRAPI::load_layer_properties() {
	// This queries additional layers that are available and can be initialized when we create our OpenXR instance
	if (layer_properties != nullptr) {
		// already retrieved this
		return true;
	}

	// Note, instance is not yet setup so we can't use get_error_string to retrieve our error
	XrResult result = xrEnumerateApiLayerProperties(0, &num_layer_properties, nullptr);
	ERR_FAIL_COND_V_MSG(XR_FAILED(result), false, "OpenXR: Failed to enumerate number of api layer properties");

	layer_properties = (XrApiLayerProperties *)memalloc(sizeof(XrApiLayerProperties) * num_layer_properties);
	ERR_FAIL_NULL_V(layer_properties, false);
	for (uint32_t i = 0; i < num_layer_properties; i++) {
		layer_properties[i].type = XR_TYPE_API_LAYER_PROPERTIES;
		layer_properties[i].next = nullptr;
	}

	result = xrEnumerateApiLayerProperties(num_layer_properties, &num_layer_properties, layer_properties);
	ERR_FAIL_COND_V_MSG(XR_FAILED(result), false, "OpenXR: Failed to enumerate api layer properties");

#ifdef DEBUG
	for (uint32_t i = 0; i < num_layer_properties; i++) {
		print_line("OpenXR: Found OpenXR layer ", layer_properties[i].layerName);
	}
#endif

	return true;
}

bool OpenXRAPI::load_supported_extensions() {
	// This queries supported extensions that are available and can be initialized when we create our OpenXR instance

	if (supported_extensions != nullptr) {
		// already retrieved this
		return true;
	}

	// Note, instance is not yet setup so we can't use get_error_string to retrieve our error
	XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &num_supported_extensions, nullptr);
	ERR_FAIL_COND_V_MSG(XR_FAILED(result), false, "OpenXR: Failed to enumerate number of extension properties");

	supported_extensions = (XrExtensionProperties *)memalloc(sizeof(XrExtensionProperties) * num_supported_extensions);
	ERR_FAIL_NULL_V(supported_extensions, false);

	// set our types
	for (uint32_t i = 0; i < num_supported_extensions; i++) {
		supported_extensions[i].type = XR_TYPE_EXTENSION_PROPERTIES;
		supported_extensions[i].next = nullptr;
	}
	result = xrEnumerateInstanceExtensionProperties(nullptr, num_supported_extensions, &num_supported_extensions, supported_extensions);
	ERR_FAIL_COND_V_MSG(XR_FAILED(result), false, "OpenXR: Failed to enumerate extension properties");

#ifdef DEBUG
	for (uint32_t i = 0; i < num_supported_extensions; i++) {
		print_line("OpenXR: Found OpenXR extension ", supported_extensions[i].extensionName);
	}
#endif

	return true;
}

bool OpenXRAPI::is_extension_supported(const String &p_extension) const {
	for (uint32_t i = 0; i < num_supported_extensions; i++) {
		if (supported_extensions[i].extensionName == p_extension) {
#ifdef DEBUG
			print_line("OpenXR: requested extension", p_extension, "is supported");
#endif
			return true;
		}
	}

#ifdef DEBUG
	print_line("OpenXR: requested extension", p_extension, "is not supported");
#endif

	return false;
}

void OpenXRAPI::copy_string_to_char_buffer(const String p_string, char *p_buffer, int p_buffer_len) {
	CharString char_string = p_string.utf8();
	int len = char_string.length();
	if (len < p_buffer_len - 1) {
		// was having weird CI issues with strcpy so....
		memcpy(p_buffer, char_string.get_data(), len);
		p_buffer[len] = '\0';
	} else {
		memcpy(p_buffer, char_string.get_data(), p_buffer_len - 1);
		p_buffer[p_buffer_len - 1] = '\0';
	}
}

bool OpenXRAPI::create_instance() {
	// Create our OpenXR instance, this will query any registered extension wrappers for extensions we need to enable.

	// Append the extensions requested by the registered extension wrappers.
	HashMap<String, bool *> requested_extensions;
	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		const HashMap<String, bool *> &wrapper_request_extensions = wrapper->get_request_extensions();

		// requested_extensions.insert(wrapper_request_extensions.begin(), wrapper_request_extensions.end());
		for (auto &requested_extension : wrapper_request_extensions) {
			requested_extensions[requested_extension.key] = requested_extension.value;
		}
	}

	// Add optional extensions for controllers that may be supported.
	// Overkill to create extension classes for this.
	requested_extensions[XR_EXT_HP_MIXED_REALITY_CONTROLLER_EXTENSION_NAME] = &ext_hp_mixed_reality_available;
	requested_extensions[XR_EXT_SAMSUNG_ODYSSEY_CONTROLLER_EXTENSION_NAME] = &ext_samsung_odyssey_available;
	requested_extensions[XR_HTC_VIVE_COSMOS_CONTROLLER_INTERACTION_EXTENSION_NAME] = &ext_vive_cosmos_available;
	requested_extensions[XR_HTC_VIVE_FOCUS3_CONTROLLER_INTERACTION_EXTENSION_NAME] = &ext_vive_focus3_available;
	requested_extensions[XR_HUAWEI_CONTROLLER_INTERACTION_EXTENSION_NAME] = &ext_huawei_controller_available;

	// Check which extensions are supported
	enabled_extensions.clear();

	for (auto &requested_extension : requested_extensions) {
		if (!is_extension_supported(requested_extension.key)) {
			if (requested_extension.value == nullptr) {
				// nullptr means this is a manditory extension so we fail
				ERR_FAIL_V_MSG(false, "OpenXR: OpenXR Runtime does not support OpenGL extension!");
			} else {
				// set this extension as not supported
				*requested_extension.value = false;
			}
		} else if (requested_extension.value != nullptr) {
			// set this extension as supported
			*requested_extension.value = true;

			// and record that we want to enable it
			enabled_extensions.push_back(requested_extension.key.ascii());
		} else {
			// record that we want to enable this
			enabled_extensions.push_back(requested_extension.key.ascii());
		}
	}

	Vector<const char *> extension_ptrs;
	for (int i = 0; i < enabled_extensions.size(); i++) {
		extension_ptrs.push_back(enabled_extensions[i].get_data());
	}

	// Get our project name
	String project_name = GLOBAL_GET("application/config/name");

	// Create our OpenXR instance
	XrApplicationInfo application_info{
		"", // applicationName, we'll set this down below
		1, // applicationVersion, we don't currently have this
		"Godot Game Engine", // engineName
		VERSION_MAJOR * 10000 + VERSION_MINOR * 100 + VERSION_PATCH, // engineVersion 4.0 -> 40000, 4.0.1 -> 40001, 4.1 -> 40100, etc.
		XR_CURRENT_API_VERSION // apiVersion
	};

	XrInstanceCreateInfo instance_create_info = {
		XR_TYPE_INSTANCE_CREATE_INFO, // type
		nullptr, // next
		0, // createFlags
		application_info, // applicationInfo
		0, // enabledApiLayerCount, need to find out if we need support for this?
		nullptr, // enabledApiLayerNames
		uint32_t(extension_ptrs.size()), // enabledExtensionCount
		extension_ptrs.ptr() // enabledExtensionNames
	};

	copy_string_to_char_buffer(project_name, instance_create_info.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE);

	XrResult result = xrCreateInstance(&instance_create_info, &instance);
	ERR_FAIL_COND_V_MSG(XR_FAILED(result), false, "Failed to create XR instance.");

	// from this point on we can use get_error_string to get more info about our errors...

	XrInstanceProperties instanceProps = {
		XR_TYPE_INSTANCE_PROPERTIES, // type;
		nullptr, // next
		0, // runtimeVersion, from here will be set by our get call
		"" // runtimeName
	};
	result = xrGetInstanceProperties(instance, &instanceProps);
	if (XR_FAILED(result)) {
		// not fatal probably
		print_line("OpenXR: Failed to get XR instance properties [", get_error_string(result), "]");
	} else {
		print_line("OpenXR: Running on OpenXR runtime: ", instanceProps.runtimeName, " ", OpenXRUtil::make_xr_version_string(instanceProps.runtimeVersion));
	}

	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		wrapper->on_instance_created(instance);
	}

	return true;
}

bool OpenXRAPI::get_system_info() {
	// Retrieve basic OpenXR system info based on the form factor we desire

	// Retrieve the system for our form factor, fails if form factor is not available
	XrSystemGetInfo system_get_info = {
		XR_TYPE_SYSTEM_GET_INFO, // type;
		nullptr, // next
		form_factor // formFactor
	};

	XrResult result = xrGetSystem(instance, &system_get_info, &system_id);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Failed to get system for our form factor [", get_error_string(result), "]");
		return false;
	}

	// obtain info about our system, writing this out completely to make CI on Linux happy..
	void *next_pointer = nullptr;
	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		void *np = wrapper->set_system_properties_and_get_next_pointer(next_pointer);
		if (np != nullptr) {
			next_pointer = np;
		}
	}

	XrSystemProperties system_properties = {
		XR_TYPE_SYSTEM_PROPERTIES, // type
		next_pointer, // next
		0, // systemId, from here will be set by our get call
		0, // vendorId
		"", // systemName
		{
				0, // maxSwapchainImageHeight
				0, // maxSwapchainImageWidth
				0, // maxLayerCount
		}, // graphicsProperties
		{
				false, // orientationTracking
				false // positionTracking
		} // trackingProperties
	};

	result = xrGetSystemProperties(instance, system_id, &system_properties);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Failed to get System properties [", get_error_string(result), "]");
		return false;
	}

	// remember this state, we'll use it later
	system_name = String(system_properties.systemName);
	vendor_id = system_properties.vendorId;
	graphics_properties = system_properties.graphicsProperties;
	tracking_properties = system_properties.trackingProperties;

	return true;
}

bool OpenXRAPI::load_supported_view_configuration_types() {
	// This queries the supported configuration types, likely there will only be one choosing between Mono (phone AR) and Stereo (HMDs)

	ERR_FAIL_COND_V(instance == XR_NULL_HANDLE, false);

	if (supported_view_configuration_types != nullptr) {
		// free previous results
		memfree(supported_view_configuration_types);
		supported_view_configuration_types = nullptr;
	}

	XrResult result = xrEnumerateViewConfigurations(instance, system_id, 0, &num_view_configuration_types, nullptr);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Failed to get view configuration count [", get_error_string(result), "]");
		return false;
	}

	supported_view_configuration_types = (XrViewConfigurationType *)memalloc(sizeof(XrViewConfigurationType) * num_view_configuration_types);
	ERR_FAIL_NULL_V(supported_view_configuration_types, false);

	result = xrEnumerateViewConfigurations(instance, system_id, num_view_configuration_types, &num_view_configuration_types, supported_view_configuration_types);
	ERR_FAIL_COND_V_MSG(XR_FAILED(result), false, "OpenXR: Failed to enumerateview configurations");

#ifdef DEBUG
	for (uint32_t i = 0; i < num_view_configuration_types; i++) {
		print_line("OpenXR: Found supported view configuration ", OpenXRUtil::get_view_configuration_name(supported_view_configuration_types[i]));
	}
#endif

	return true;
}

bool OpenXRAPI::is_view_configuration_supported(XrViewConfigurationType p_configuration_type) const {
	ERR_FAIL_NULL_V(supported_view_configuration_types, false);

	for (uint32_t i = 0; i < num_view_configuration_types; i++) {
		if (supported_view_configuration_types[i] == p_configuration_type) {
			return true;
		}
	}

	return false;
}

bool OpenXRAPI::load_supported_view_configuration_views(XrViewConfigurationType p_configuration_type) {
	// This loads our view configuration for each view so for a stereo HMD, we'll get two entries (that are likely identical)
	// The returned data supplies us with the recommended render target size

	if (!is_view_configuration_supported(p_configuration_type)) {
		print_line("OpenXR: View configuration ", OpenXRUtil::get_view_configuration_name(view_configuration), " is not supported.");
		return false;
	}

	if (view_configuration_views != nullptr) {
		// free previous results
		memfree(view_configuration_views);
		view_configuration_views = nullptr;
	}

	XrResult result = xrEnumerateViewConfigurationViews(instance, system_id, p_configuration_type, 0, &view_count, nullptr);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Failed to get view configuration count [", get_error_string(result), "]");
		return false;
	}

	view_configuration_views = (XrViewConfigurationView *)memalloc(sizeof(XrViewConfigurationView) * view_count);
	ERR_FAIL_NULL_V(view_configuration_views, false);

	for (uint32_t i = 0; i < view_count; i++) {
		view_configuration_views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		view_configuration_views[i].next = nullptr;
	}

	result = xrEnumerateViewConfigurationViews(instance, system_id, p_configuration_type, view_count, &view_count, view_configuration_views);
	ERR_FAIL_COND_V_MSG(XR_FAILED(result), false, "OpenXR: Failed to enumerate view configurations");

#ifdef DEBUG
	for (uint32_t i = 0; i < view_count; i++) {
		print_line("OpenXR: Found supported view configuration view");
		print_line(" - width: ", view_configuration_views[i].maxImageRectWidth);
		print_line(" - height: ", view_configuration_views[i].maxImageRectHeight);
		print_line(" - sample count: ", view_configuration_views[i].maxSwapchainSampleCount);
		print_line(" - recommended render width: ", view_configuration_views[i].recommendedImageRectWidth);
		print_line(" - recommended render height: ", view_configuration_views[i].recommendedImageRectHeight);
		print_line(" - recommended render sample count: ", view_configuration_views[i].recommendedSwapchainSampleCount);
	}
#endif

	return true;
}

void OpenXRAPI::destroy_instance() {
	if (view_configuration_views != nullptr) {
		memfree(view_configuration_views);
		view_configuration_views = nullptr;
	}

	if (supported_view_configuration_types != nullptr) {
		memfree(supported_view_configuration_types);
		supported_view_configuration_types = nullptr;
	}

	if (instance != XR_NULL_HANDLE) {
		for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
			wrapper->on_instance_destroyed();
		}

		xrDestroyInstance(instance);
		instance = XR_NULL_HANDLE;
	}
	enabled_extensions.clear();
}

bool OpenXRAPI::create_session() {
	ERR_FAIL_COND_V(instance == XR_NULL_HANDLE, false);
	ERR_FAIL_COND_V(session != XR_NULL_HANDLE, false);

	void *next_pointer = nullptr;
	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		void *np = wrapper->set_session_create_and_get_next_pointer(next_pointer);
		if (np != nullptr) {
			next_pointer = np;
		}
	}

	XrSessionCreateInfo session_create_info = {
		XR_TYPE_SESSION_CREATE_INFO, // type
		next_pointer, // next
		0, // createFlags
		system_id // systemId
	};

	XrResult result = xrCreateSession(instance, &session_create_info, &session);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Failed to create session [", get_error_string(result), "]");
		return false;
	}

	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		wrapper->on_session_created(session);
	}

	return true;
}

bool OpenXRAPI::load_supported_reference_spaces() {
	// loads the supported reference spaces for our OpenXR session

	ERR_FAIL_COND_V(session == XR_NULL_HANDLE, false);

	if (supported_reference_spaces != nullptr) {
		// free previous results
		memfree(supported_reference_spaces);
		supported_reference_spaces = nullptr;
	}

	XrResult result = xrEnumerateReferenceSpaces(session, 0, &num_reference_spaces, nullptr);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Failed to get reference space count [", get_error_string(result), "]");
		return false;
	}

	supported_reference_spaces = (XrReferenceSpaceType *)memalloc(sizeof(XrReferenceSpaceType) * num_reference_spaces);
	ERR_FAIL_NULL_V(supported_reference_spaces, false);

	result = xrEnumerateReferenceSpaces(session, num_reference_spaces, &num_reference_spaces, supported_reference_spaces);
	ERR_FAIL_COND_V_MSG(XR_FAILED(result), false, "OpenXR: Failed to enumerate reference spaces");

	// #ifdef DEBUG
	for (uint32_t i = 0; i < num_reference_spaces; i++) {
		print_line("OpenXR: Found supported reference space ", OpenXRUtil::get_reference_space_name(supported_reference_spaces[i]));
	}
	// #endif

	return true;
}

bool OpenXRAPI::is_reference_space_supported(XrReferenceSpaceType p_reference_space) {
	ERR_FAIL_NULL_V(supported_reference_spaces, false);

	for (uint32_t i = 0; i < num_reference_spaces; i++) {
		if (supported_reference_spaces[i] == p_reference_space) {
			return true;
		}
	}

	return false;
}

bool OpenXRAPI::setup_spaces() {
	XrResult result;

	XrPosef identityPose = {
		{ 0.0, 0.0, 0.0, 1.0 },
		{ 0.0, 0.0, 0.0 }
	};

	ERR_FAIL_COND_V(session == XR_NULL_HANDLE, false);

	// create play space
	{
		if (!is_reference_space_supported(reference_space)) {
			print_line("OpenXR: reference space ", OpenXRUtil::get_reference_space_name(reference_space), " is not supported.");
			return false;
		}

		XrReferenceSpaceCreateInfo play_space_create_info = {
			XR_TYPE_REFERENCE_SPACE_CREATE_INFO, // type
			nullptr, // next
			reference_space, // referenceSpaceType
			identityPose // poseInReferenceSpace
		};

		result = xrCreateReferenceSpace(session, &play_space_create_info, &play_space);
		if (XR_FAILED(result)) {
			print_line("OpenXR: Failed to create play space [", get_error_string(result), "]");
			return false;
		}
	}

	// create view space
	{
		if (!is_reference_space_supported(XR_REFERENCE_SPACE_TYPE_VIEW)) {
			print_line("OpenXR: reference space XR_REFERENCE_SPACE_TYPE_VIEW is not supported.");
			return false;
		}

		XrReferenceSpaceCreateInfo view_space_create_info = {
			XR_TYPE_REFERENCE_SPACE_CREATE_INFO, // type
			nullptr, // next
			XR_REFERENCE_SPACE_TYPE_VIEW, // referenceSpaceType
			identityPose // poseInReferenceSpace
		};

		result = xrCreateReferenceSpace(session, &view_space_create_info, &view_space);
		if (XR_FAILED(result)) {
			print_line("OpenXR: Failed to create view space [", get_error_string(result), "]");
			return false;
		}
	}

	return true;
}

bool OpenXRAPI::load_supported_swapchain_formats() {
	ERR_FAIL_COND_V(session == XR_NULL_HANDLE, false);

	if (supported_swapchain_formats != nullptr) {
		// free previous results
		memfree(supported_swapchain_formats);
		supported_swapchain_formats = nullptr;
	}

	XrResult result = xrEnumerateSwapchainFormats(session, 0, &num_swapchain_formats, nullptr);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Failed to get swapchain format count [", get_error_string(result), "]");
		return false;
	}

	supported_swapchain_formats = (int64_t *)memalloc(sizeof(int64_t) * num_swapchain_formats);
	ERR_FAIL_NULL_V(supported_swapchain_formats, false);

	result = xrEnumerateSwapchainFormats(session, num_swapchain_formats, &num_swapchain_formats, supported_swapchain_formats);
	ERR_FAIL_COND_V_MSG(XR_FAILED(result), false, "OpenXR: Failed to enumerate swapchain formats");

	// #ifdef DEBUG
	for (uint32_t i = 0; i < num_swapchain_formats; i++) {
		print_line("OpenXR: Found supported swapchain format ", get_swapchain_format_name(supported_swapchain_formats[i]));
	}
	// #endif

	return true;
}

bool OpenXRAPI::is_swapchain_format_supported(int64_t p_swapchain_format) {
	ERR_FAIL_NULL_V(supported_swapchain_formats, false);

	for (uint32_t i = 0; i < num_swapchain_formats; i++) {
		if (supported_swapchain_formats[i] == p_swapchain_format) {
			return true;
		}
	}

	return false;
}

bool OpenXRAPI::create_main_swapchain() {
	ERR_FAIL_NULL_V(graphics_extension, false);
	ERR_FAIL_COND_V(session == XR_NULL_HANDLE, false);

	/*
		TODO: We need to improve on this, for now we're taking our old approach of creating our main swapchains and substituting
		those for the ones Godot normally creates.
		This however means we can only use swapchains for our main XR view.

		It would have been nicer if we could override the swapchain creation in Godot with ours but we have a timing issue here.
		We can't create XR swapchains until after our XR session is fully instantiated, yet Godot creates its swapchain much earlier.

		Also Godot only creates a swapchain for the main output.
		OpenXR will require us to create swapchains as the render target for additional viewports if we want to use the layer system
		to optimise text rendering and background rendering as OpenXR may choose to re-use the results for reprojection while we're
		already rendering the next frame.

		Finally an area we need to expand upon is that Foveated rendering is only enabled for the swap chain we create,
		as we render 3D content into internal buffers that are copied into the swapchain, we don't get any of the performance gains
		until such time as we implement VRS.
	*/

	// Build a vector with swapchain formats we want to use, from best fit to worst
	Vector<int64_t> usable_swapchain_formats;
	int64_t swapchain_format_to_use = 0;

	graphics_extension->get_usable_swapchain_formats(usable_swapchain_formats);

	// now find out which one is supported
	for (int i = 0; i < usable_swapchain_formats.size() && swapchain_format_to_use == 0; i++) {
		if (is_swapchain_format_supported(usable_swapchain_formats[i])) {
			swapchain_format_to_use = usable_swapchain_formats[i];
		}
	}

	if (swapchain_format_to_use == 0) {
		swapchain_format_to_use = usable_swapchain_formats[0]; // just use the first one and hope for the best...
		print_line("Couldn't find usable swap chain format, using", get_swapchain_format_name(swapchain_format_to_use), "instead.");
	} else {
		print_line("Using swap chain format:", get_swapchain_format_name(swapchain_format_to_use));
	}

	Size2 recommended_size = get_recommended_target_size();

	if (!create_swapchain(swapchain_format_to_use, recommended_size.width, recommended_size.height, view_configuration_views[0].recommendedSwapchainSampleCount, view_count, swapchain, &swapchain_graphics_data)) {
		return false;
	}

	views = (XrView *)memalloc(sizeof(XrView) * view_count);
	ERR_FAIL_NULL_V_MSG(views, false, "OpenXR Couldn't allocate memory for views");

	projection_views = (XrCompositionLayerProjectionView *)memalloc(sizeof(XrCompositionLayerProjectionView) * view_count);
	ERR_FAIL_NULL_V_MSG(projection_views, false, "OpenXR Couldn't allocate memory for projection views");

	for (uint32_t i = 0; i < view_count; i++) {
		views[i].type = XR_TYPE_VIEW;
		views[i].next = nullptr;

		projection_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		projection_views[i].next = nullptr;
		projection_views[i].subImage.swapchain = swapchain;
		projection_views[i].subImage.imageArrayIndex = i;
		projection_views[i].subImage.imageRect.offset.x = 0;
		projection_views[i].subImage.imageRect.offset.y = 0;
		projection_views[i].subImage.imageRect.extent.width = recommended_size.width;
		projection_views[i].subImage.imageRect.extent.height = recommended_size.height;
	};

	return true;
};

void OpenXRAPI::destroy_session() {
	if (running && session != XR_NULL_HANDLE) {
		xrEndSession(session);
	}

	if (graphics_extension) {
		graphics_extension->cleanup_swapchain_graphics_data(&swapchain_graphics_data);
	}

	if (views != nullptr) {
		memfree(views);
		views = nullptr;
	}

	if (projection_views != nullptr) {
		memfree(projection_views);
		projection_views = nullptr;
	}

	if (swapchain != XR_NULL_HANDLE) {
		xrDestroySwapchain(swapchain);
		swapchain = XR_NULL_HANDLE;
	}

	if (supported_swapchain_formats != nullptr) {
		memfree(supported_swapchain_formats);
		supported_swapchain_formats = nullptr;
	}

	// destroy our spaces
	if (play_space != XR_NULL_HANDLE) {
		xrDestroySpace(play_space);
		play_space = XR_NULL_HANDLE;
	}
	if (view_space != XR_NULL_HANDLE) {
		xrDestroySpace(view_space);
		view_space = XR_NULL_HANDLE;
	}

	if (supported_reference_spaces != nullptr) {
		// free previous results
		memfree(supported_reference_spaces);
		supported_reference_spaces = nullptr;
	}

	if (session != XR_NULL_HANDLE) {
		for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
			wrapper->on_session_destroyed();
		}

		xrDestroySession(session);
		session = XR_NULL_HANDLE;
	}
}

bool OpenXRAPI::create_swapchain(int64_t p_swapchain_format, uint32_t p_width, uint32_t p_height, uint32_t p_sample_count, uint32_t p_array_size, XrSwapchain &r_swapchain, void **r_swapchain_graphics_data) {
	ERR_FAIL_COND_V(session == XR_NULL_HANDLE, false);
	ERR_FAIL_NULL_V(graphics_extension, false);

	XrResult result;

	void *next_pointer = nullptr;
	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		void *np = wrapper->set_swapchain_create_info_and_get_next_pointer(next_pointer);
		if (np != nullptr) {
			next_pointer = np;
		}
	}

	XrSwapchainCreateInfo swapchain_create_info = {
		XR_TYPE_SWAPCHAIN_CREATE_INFO, // type
		next_pointer, // next
		0, // createFlags
		XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT, // usageFlags
		p_swapchain_format, // format
		p_sample_count, // sampleCount
		p_width, // width
		p_height, // height
		1, // faceCount
		p_array_size, // arraySize
		1 // mipCount
	};

	XrSwapchain new_swapchain;
	result = xrCreateSwapchain(session, &swapchain_create_info, &new_swapchain);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Failed to get swapchain [", get_error_string(result), "]");
		return false;
	}

	if (!graphics_extension->get_swapchain_image_data(new_swapchain, p_swapchain_format, p_width, p_height, p_sample_count, p_array_size, r_swapchain_graphics_data)) {
		xrDestroySwapchain(new_swapchain);
		return false;
	}

	r_swapchain = new_swapchain;

	return true;
}

bool OpenXRAPI::on_state_idle() {
#ifdef DEBUG
	print_line("On state idle");
#endif

	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		wrapper->on_state_idle();
	}

	return true;
}

bool OpenXRAPI::on_state_ready() {
#ifdef DEBUG
	print_line("On state ready");
#endif

	// begin session
	XrSessionBeginInfo session_begin_info = {
		XR_TYPE_SESSION_BEGIN_INFO, // type
		nullptr, // next
		view_configuration // primaryViewConfigurationType
	};

	XrResult result = xrBeginSession(session, &session_begin_info);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Failed to begin session [", get_error_string(result), "]");
		return false;
	}

	// This is when we create our swapchain, this can be a "long" time after Godot finishes, we can deal with this for now
	// but once we want to provide Viewports for additional layers where OpenXR requires us to create further swapchains,
	// we'll be creating those viewport WAY before we reach this point.
	// We may need to implement a wait in our init in main.cpp polling our events until the session is ready.
	// That will be very very ugly
	// The other possibility is to create a separate OpenXRViewport type specifically for this goal as part of our OpenXR module

	if (!create_main_swapchain()) {
		return false;
	}

	// we're running
	running = true;

	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		wrapper->on_state_ready();
	}

	if (xr_interface) {
		xr_interface->on_state_ready();
	}

	// TODO Tell android

	return true;
}

bool OpenXRAPI::on_state_synchronized() {
#ifdef DEBUG
	print_line("On state synchronized");
#endif

	// Just in case, see if we already have active trackers...
	List<RID> trackers;
	tracker_owner.get_owned_list(&trackers);
	for (int i = 0; i < trackers.size(); i++) {
		tracker_check_profile(trackers[i]);
	}

	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		wrapper->on_state_synchronized();
	}

	return true;
}

bool OpenXRAPI::on_state_visible() {
#ifdef DEBUG
	print_line("On state visible");
#endif

	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		wrapper->on_state_visible();
	}

	if (xr_interface) {
		xr_interface->on_state_visible();
	}

	return true;
}

bool OpenXRAPI::on_state_focused() {
#ifdef DEBUG
	print_line("On state focused");
#endif

	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		wrapper->on_state_focused();
	}

	if (xr_interface) {
		xr_interface->on_state_focused();
	}

	return true;
}

bool OpenXRAPI::on_state_stopping() {
#ifdef DEBUG
	print_line("On state stopping");
#endif

	if (xr_interface) {
		xr_interface->on_state_stopping();
	}

	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		wrapper->on_state_stopping();
	}

	if (running) {
		XrResult result = xrEndSession(session);
		if (XR_FAILED(result)) {
			// we only report this..
			print_line("OpenXR: Failed to end session [", get_error_string(result), "]");
		}

		running = false;
	}

	// TODO further cleanup

	return true;
}

bool OpenXRAPI::on_state_loss_pending() {
#ifdef DEBUG
	print_line("On state loss pending");
#endif

	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		wrapper->on_state_loss_pending();
	}

	// TODO need to look into the correct action here, read up on the spec but we may need to signal Godot to exit (if it's not already exiting)

	return true;
}

bool OpenXRAPI::on_state_exiting() {
#ifdef DEBUG
	print_line("On state existing");
#endif

	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		wrapper->on_state_exiting();
	}

	// TODO need to look into the correct action here, read up on the spec but we may need to signal Godot to exit (if it's not already exiting)

	return true;
}

bool OpenXRAPI::is_initialized() {
	return (instance != XR_NULL_HANDLE);
}

bool OpenXRAPI::is_running() {
	if (instance == XR_NULL_HANDLE) {
		return false;
	}
	if (session == XR_NULL_HANDLE) {
		return false;
	}

	return running;
}

bool OpenXRAPI::initialize(const String &p_rendering_driver) {
	ERR_FAIL_COND_V_MSG(instance != XR_NULL_HANDLE, false, "OpenXR instance was already created");

	if (p_rendering_driver == "vulkan") {
#ifdef VULKAN_ENABLED
		graphics_extension = memnew(OpenXRVulkanExtension(this));
		register_extension_wrapper(graphics_extension);
#else
		// shouldn't be possible...
		ERR_FAIL_V(false);
#endif
	} else if (p_rendering_driver == "opengl3") {
#ifdef GLES3_ENABLED
		// graphics_extension = memnew(OpenXROpenGLExtension(this));
		// register_extension_wrapper(graphics_extension);
		ERR_FAIL_V_MSG(false, "OpenXR: OpenGL is not supported at this time.");
#else
		// shouldn't be possible...
		ERR_FAIL_V(false);
#endif
	} else {
		ERR_FAIL_V_MSG(false, "OpenXR: Unsupported rendering device.");
	}

	// initialize
	if (!load_layer_properties()) {
		destroy_instance();
		return false;
	}

	if (!load_supported_extensions()) {
		destroy_instance();
		return false;
	}

	if (!create_instance()) {
		destroy_instance();
		return false;
	}

	if (!get_system_info()) {
		destroy_instance();
		return false;
	}

	if (!load_supported_view_configuration_types()) {
		destroy_instance();
		return false;
	}

	if (!load_supported_view_configuration_views(view_configuration)) {
		destroy_instance();
		return false;
	}

	return true;
}

bool OpenXRAPI::initialize_session() {
	if (!create_session()) {
		destroy_session();
		return false;
	}

	if (!load_supported_reference_spaces()) {
		destroy_session();
		return false;
	}

	if (!setup_spaces()) {
		destroy_session();
		return false;
	}

	if (!load_supported_swapchain_formats()) {
		destroy_session();
		return false;
	}

	return true;
}

void OpenXRAPI::finish() {
	destroy_session();

	destroy_instance();
}

void OpenXRAPI::set_xr_interface(OpenXRInterface *p_xr_interface) {
	xr_interface = p_xr_interface;
}

void OpenXRAPI::register_extension_wrapper(OpenXRExtensionWrapper *p_extension_wrapper) {
	registered_extension_wrappers.push_back(p_extension_wrapper);
}

Size2 OpenXRAPI::get_recommended_target_size() {
	ERR_FAIL_NULL_V(view_configuration_views, Size2());

	Size2 target_size;

	target_size.width = view_configuration_views[0].recommendedImageRectWidth;
	target_size.height = view_configuration_views[0].recommendedImageRectHeight;

	return target_size;
}

XRPose::TrackingConfidence OpenXRAPI::get_head_center(Transform3D &r_transform, Vector3 &r_linear_velocity, Vector3 &r_angular_velocity) {
	XrResult result;

	ERR_FAIL_COND_V(!running, XRPose::XR_TRACKING_CONFIDENCE_NONE);

	// xrWaitFrame not run yet
	if (frame_state.predictedDisplayTime == 0) {
		return XRPose::XR_TRACKING_CONFIDENCE_NONE;
	}

	// Get timing for the next frame, as that is the current frame we're processing
	XrTime display_time = get_next_frame_time();

	XrSpaceVelocity velocity = {
		XR_TYPE_SPACE_VELOCITY, // type
		nullptr, // next
		0, // velocityFlags
		{ 0.0, 0.0, 0.0 }, // linearVelocity
		{ 0.0, 0.0, 0.0 } // angularVelocity
	};

	XrSpaceLocation location = {
		XR_TYPE_SPACE_LOCATION, // type
		&velocity, // next
		0, // locationFlags
		{
				{ 0.0, 0.0, 0.0, 0.0 }, // orientation
				{ 0.0, 0.0, 0.0 } // position
		} // pose
	};

	result = xrLocateSpace(view_space, play_space, display_time, &location);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Failed to locate view space in play space [", get_error_string(result), "]");
		return XRPose::XR_TRACKING_CONFIDENCE_NONE;
	}

	XRPose::TrackingConfidence confidence = transform_from_location(location, r_transform);
	parse_velocities(velocity, r_linear_velocity, r_angular_velocity);

	if (head_pose_confidence != confidence) {
		// prevent error spam
		head_pose_confidence = confidence;
		if (head_pose_confidence == XRPose::XR_TRACKING_CONFIDENCE_NONE) {
			print_line("OpenXR head space location not valid (check tracking?)");
#ifdef DEBUG
		} else if (head_pose_confidence == XRPose::XR_TRACKING_CONFIDENCE_LOW) {
			print_line("OpenVR Head pose now tracking with low confidence");
		} else {
			print_line("OpenVR Head pose now tracking with high confidence");
#endif
		}
	}

	return confidence;
}

bool OpenXRAPI::get_view_transform(uint32_t p_view, Transform3D &r_transform) {
	ERR_FAIL_COND_V(!running, false);

	// xrWaitFrame not run yet
	if (frame_state.predictedDisplayTime == 0) {
		return false;
	}

	// we don't have valid view info
	if (views == nullptr || !view_pose_valid) {
		return false;
	}

	// Note, the timing of this is set right before rendering, which is what we need here.
	r_transform = transform_from_pose(views[p_view].pose);

	return true;
}

bool OpenXRAPI::get_view_projection(uint32_t p_view, double p_z_near, double p_z_far, Projection &p_camera_matrix) {
	ERR_FAIL_COND_V(!running, false);
	ERR_FAIL_NULL_V(graphics_extension, false);

	// xrWaitFrame not run yet
	if (frame_state.predictedDisplayTime == 0) {
		return false;
	}

	// we don't have valid view info
	if (views == nullptr || !view_pose_valid) {
		return false;
	}

	return graphics_extension->create_projection_fov(views[p_view].fov, p_z_near, p_z_far, p_camera_matrix);
}

bool OpenXRAPI::poll_events() {
	ERR_FAIL_COND_V(instance == XR_NULL_HANDLE, false);

	XrEventDataBuffer runtimeEvent;
	runtimeEvent.type = XR_TYPE_EVENT_DATA_BUFFER;
	runtimeEvent.next = nullptr;
	// runtimeEvent.varying = ...

	XrResult pollResult = xrPollEvent(instance, &runtimeEvent);
	while (pollResult == XR_SUCCESS) {
		bool handled = false;
		for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
			handled |= wrapper->on_event_polled(runtimeEvent);
		}
		switch (runtimeEvent.type) {
			case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
				XrEventDataEventsLost *event = (XrEventDataEventsLost *)&runtimeEvent;

				// We probably didn't poll fast enough, just output warning
				WARN_PRINT("OpenXR EVENT: " + itos(event->lostEventCount) + " event data lost!");
			} break;
			case XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR: {
				// XrEventDataVisibilityMaskChangedKHR *event = (XrEventDataVisibilityMaskChangedKHR *)&runtimeEvent;

				// TODO implement this in the future, we should call xrGetVisibilityMaskKHR to obtain a mask,
				// this will allow us to prevent rendering the part of our view which is never displayed giving us
				// a decent performance improvement.

				print_verbose("OpenXR EVENT: STUB: visibility mask changed");
			} break;
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				XrEventDataInstanceLossPending *event = (XrEventDataInstanceLossPending *)&runtimeEvent;

				// TODO We get this event if we're about to loose our OpenXR instance.
				// We should queue exiting Godot at this point.

				print_verbose("OpenXR EVENT: instance loss pending at " + itos(event->lossTime));
				return false;
			} break;
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				XrEventDataSessionStateChanged *event = (XrEventDataSessionStateChanged *)&runtimeEvent;

				session_state = event->state;
				if (session_state >= XR_SESSION_STATE_MAX_ENUM) {
					print_verbose("OpenXR EVENT: session state changed to UNKNOWN - " + itos(session_state));
				} else {
					print_verbose("OpenXR EVENT: session state changed to " + OpenXRUtil::get_session_state_name(session_state));

					switch (session_state) {
						case XR_SESSION_STATE_IDLE:
							on_state_idle();
							break;
						case XR_SESSION_STATE_READY:
							on_state_ready();
							break;
						case XR_SESSION_STATE_SYNCHRONIZED:
							on_state_synchronized();
							break;
						case XR_SESSION_STATE_VISIBLE:
							on_state_visible();
							break;
						case XR_SESSION_STATE_FOCUSED:
							on_state_focused();
							break;
						case XR_SESSION_STATE_STOPPING:
							on_state_stopping();
							break;
						case XR_SESSION_STATE_LOSS_PENDING:
							on_state_loss_pending();
							break;
						case XR_SESSION_STATE_EXITING:
							on_state_exiting();
							break;
						default:
							break;
					}
				}
			} break;
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				XrEventDataReferenceSpaceChangePending *event = (XrEventDataReferenceSpaceChangePending *)&runtimeEvent;

				print_verbose("OpenXR EVENT: reference space type " + OpenXRUtil::get_reference_space_name(event->referenceSpaceType) + " change pending!");
				if (event->poseValid && xr_interface) {
					xr_interface->on_pose_recentered();
				}
			} break;
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
				print_verbose("OpenXR EVENT: interaction profile changed!");

				XrEventDataInteractionProfileChanged *event = (XrEventDataInteractionProfileChanged *)&runtimeEvent;

				List<RID> trackers;
				tracker_owner.get_owned_list(&trackers);
				for (int i = 0; i < trackers.size(); i++) {
					tracker_check_profile(trackers[i], event->session);
				}

			} break;
			default:
				if (!handled) {
					print_verbose("OpenXR Unhandled event type " + OpenXRUtil::get_structure_type_name(runtimeEvent.type));
				}
				break;
		}

		runtimeEvent.type = XR_TYPE_EVENT_DATA_BUFFER;
		pollResult = xrPollEvent(instance, &runtimeEvent);
	}

	if (pollResult == XR_EVENT_UNAVAILABLE) {
		// processed all events in the queue
		return true;
	} else {
		ERR_FAIL_V_MSG(false, "OpenXR: Failed to poll events!");
	}
}

bool OpenXRAPI::process() {
	ERR_FAIL_COND_V(instance == XR_NULL_HANDLE, false);

	if (!poll_events()) {
		return false;
	}

	if (!running) {
		return false;
	}

	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		wrapper->on_process();
	}

	return true;
}

bool OpenXRAPI::acquire_image(XrSwapchain p_swapchain, uint32_t &r_image_index) {
	ERR_FAIL_COND_V(image_acquired, true); // this was not released when it should be, error out and re-use...

	XrResult result;
	XrSwapchainImageAcquireInfo swapchain_image_acquire_info = {
		XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, // type
		nullptr // next
	};
	result = xrAcquireSwapchainImage(p_swapchain, &swapchain_image_acquire_info, &r_image_index);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to acquire swapchain image [", get_error_string(result), "]");
		return false;
	}

	XrSwapchainImageWaitInfo swapchain_image_wait_info = {
		XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, // type
		nullptr, // next
		17000000 // timeout in nanoseconds
	};

	result = xrWaitSwapchainImage(p_swapchain, &swapchain_image_wait_info);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to wait for swapchain image [", get_error_string(result), "]");
		return false;
	}

	return true;
}

bool OpenXRAPI::release_image(XrSwapchain p_swapchain) {
	XrSwapchainImageReleaseInfo swapchain_image_release_info = {
		XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, // type
		nullptr // next
	};
	XrResult result = xrReleaseSwapchainImage(swapchain, &swapchain_image_release_info);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to release swapchain image! [", get_error_string(result), "]");
		return false;
	}

	return true;
}

void OpenXRAPI::pre_render() {
	ERR_FAIL_COND(instance == XR_NULL_HANDLE);

	if (!running) {
		return;
	}

	// Waitframe does 2 important things in our process:
	// 1) It provides us with predictive timing, telling us when OpenXR expects to display the frame we're about to commit
	// 2) It will use the previous timing to pause our thread so that rendering starts as close to displaying as possible
	// This must thus be called as close to when we start rendering as possible
	XrFrameWaitInfo frame_wait_info = { XR_TYPE_FRAME_WAIT_INFO, nullptr };
	XrResult result = xrWaitFrame(session, &frame_wait_info, &frame_state);
	if (XR_FAILED(result)) {
		print_line("OpenXR: xrWaitFrame() was not successful [", get_error_string(result), "]");

		// reset just in case
		frame_state.predictedDisplayTime = 0;
		frame_state.predictedDisplayPeriod = 0;
		frame_state.shouldRender = false;

		return;
	}

	if (frame_state.predictedDisplayPeriod > 500000000) {
		// display period more then 0.5 seconds? must be wrong data
		print_verbose("OpenXR resetting invalid display period " + rtos(frame_state.predictedDisplayPeriod));
		frame_state.predictedDisplayPeriod = 0;
	}

	for (OpenXRExtensionWrapper *wrapper : registered_extension_wrappers) {
		wrapper->on_pre_render();
	}

	// Get our view info for the frame we're about to render, note from the OpenXR manual:
	// "Repeatedly calling xrLocateViews with the same time may not necessarily return the same result. Instead the prediction gets increasingly accurate as the function is called closer to the given time for which a prediction is made"

	// We're calling this "relatively" early, the positioning we're obtaining here will be used to do our frustum culling,
	// occlusion culling, etc. There is however a technique that we can investigate in the future where after our entire
	// Vulkan command buffer is build, but right before vkSubmitQueue is called, we call xrLocateViews one more time and
	// update the view and projection matrix once more with a slightly more accurate predication and then submit the
	// command queues.

	// That is not possible yet but worth investigating in the future.

	XrViewLocateInfo view_locate_info = {
		XR_TYPE_VIEW_LOCATE_INFO, // type
		nullptr, // next
		view_configuration, // viewConfigurationType
		frame_state.predictedDisplayTime, // displayTime
		play_space // space
	};
	XrViewState view_state = {
		XR_TYPE_VIEW_STATE, // type
		nullptr, // next
		0 // viewStateFlags
	};
	uint32_t view_count_output;
	result = xrLocateViews(session, &view_locate_info, &view_state, view_count, &view_count_output, views);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Couldn't locate views [", get_error_string(result), "]");
		return;
	}

	bool pose_valid = true;
	for (uint64_t i = 0; i < view_count_output; i++) {
		if ((view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0 ||
				(view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0) {
			pose_valid = false;
		}
	}
	if (view_pose_valid != pose_valid) {
		view_pose_valid = pose_valid;
#ifdef DEBUG
		if (!view_pose_valid) {
			print_line("OpenXR View pose became invalid");
		} else {
			print_line("OpenXR View pose became valid");
		}
#endif
	}

	// let's start our frame..
	XrFrameBeginInfo frame_begin_info = {
		XR_TYPE_FRAME_BEGIN_INFO, // type
		nullptr // next
	};
	result = xrBeginFrame(session, &frame_begin_info);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to being frame [", get_error_string(result), "]");
		return;
	}
}

bool OpenXRAPI::pre_draw_viewport(RID p_render_target) {
	if (!can_render()) {
		return false;
	}

	// TODO: at some point in time we may support multiple viewports in which case we need to handle that...

	return true;
}

void OpenXRAPI::post_draw_viewport(RID p_render_target) {
	if (!can_render()) {
		return;
	}

	// TODO: at some point in time we may support multiple viewports in which case we need to handle that...

	// TODO: if we can get PR 51179 to work properly we can change away from this approach and move this into get_external_texture or something
	if (!image_acquired) {
		if (!acquire_image(swapchain, image_index)) {
			return;
		}
		image_acquired = true;

		// print_line("OpenXR: acquired image " + itos(image_index) + ", copying...");

		// Copy our buffer into our swap chain (remove once PR 51179 is done)
		graphics_extension->copy_render_target_to_image(p_render_target, swapchain_graphics_data, image_index);
	}
};

void OpenXRAPI::end_frame() {
	XrResult result;

	ERR_FAIL_COND(instance == XR_NULL_HANDLE);

	if (!running) {
		return;
	}

	if (frame_state.shouldRender && view_pose_valid && !image_acquired) {
		print_line("OpenXR: No viewport was marked with use_xr, there is no rendered output!");
	}

	// must have:
	// - shouldRender set to true
	// - a valid view pose for projection_views[eye].pose to submit layer
	// - an image to render
	if (!frame_state.shouldRender || !view_pose_valid || !image_acquired) {
		// submit 0 layers when we shouldn't render
		XrFrameEndInfo frame_end_info = {
			XR_TYPE_FRAME_END_INFO, // type
			nullptr, // next
			frame_state.predictedDisplayTime, // displayTime
			XR_ENVIRONMENT_BLEND_MODE_OPAQUE, // environmentBlendMode
			0, // layerCount
			nullptr // layers
		};
		result = xrEndFrame(session, &frame_end_info);
		if (XR_FAILED(result)) {
			print_line("OpenXR: failed to end frame! [", get_error_string(result), "]");
			return;
		}

		// neither eye is rendered
		return;
	}

	// release our swapchain image if we acquired it
	if (image_acquired) {
		image_acquired = false; // whether we succeed or not, consider this released.

		release_image(swapchain);
	}

	for (uint32_t eye = 0; eye < view_count; eye++) {
		projection_views[eye].fov = views[eye].fov;
		projection_views[eye].pose = views[eye].pose;
	}

	Vector<const XrCompositionLayerBaseHeader *> layers_list;

	// Add composition layers from providers
	for (OpenXRCompositionLayerProvider *provider : composition_layer_providers) {
		XrCompositionLayerBaseHeader *layer = provider->get_composition_layer();
		if (layer) {
			layers_list.push_back(layer);
		}
	}

	XrCompositionLayerProjection projection_layer = {
		XR_TYPE_COMPOSITION_LAYER_PROJECTION, // type
		nullptr, // next
		layers_list.size() > 1 ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT : XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT, // layerFlags
		play_space, // space
		view_count, // viewCount
		projection_views, // views
	};
	layers_list.push_back((const XrCompositionLayerBaseHeader *)&projection_layer);

	XrFrameEndInfo frame_end_info = {
		XR_TYPE_FRAME_END_INFO, // type
		nullptr, // next
		frame_state.predictedDisplayTime, // displayTime
		XR_ENVIRONMENT_BLEND_MODE_OPAQUE, // environmentBlendMode
		static_cast<uint32_t>(layers_list.size()), // layerCount
		layers_list.ptr() // layers
	};
	result = xrEndFrame(session, &frame_end_info);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to end frame! [", get_error_string(result), "]");
		return;
	}
}

OpenXRAPI::OpenXRAPI() {
	// OpenXRAPI is only constructed if OpenXR is enabled.
	singleton = this;

	if (Engine::get_singleton()->is_editor_hint()) {
		// Enabled OpenXR in the editor? Adjust our settings for the editor

	} else {
		// Load settings from project settings
		int ff = GLOBAL_GET("xr/openxr/form_factor");
		switch (ff) {
			case 0: {
				form_factor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
			} break;
			case 1: {
				form_factor = XR_FORM_FACTOR_HANDHELD_DISPLAY;
			} break;
			default:
				break;
		}

		int vc = GLOBAL_GET("xr/openxr/view_configuration");
		switch (vc) {
			case 0: {
				view_configuration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO;
			} break;
			case 1: {
				view_configuration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
			} break;
			/* we don't support quad and observer configurations (yet)
			case 2: {
				view_configuration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO;
			} break;
			case 3: {
				view_configuration = XR_VIEW_CONFIGURATION_TYPE_SECONDARY_MONO_FIRST_PERSON_OBSERVER_MSFT;
			} break;
			*/
			default:
				break;
		}

		int rs = GLOBAL_GET("xr/openxr/reference_space");
		switch (rs) {
			case 0: {
				reference_space = XR_REFERENCE_SPACE_TYPE_LOCAL;
			} break;
			case 1: {
				reference_space = XR_REFERENCE_SPACE_TYPE_STAGE;
			} break;
			default:
				break;
		}
	}

	// reset a few things that can't be done in our class definition
	frame_state.predictedDisplayTime = 0;
	frame_state.predictedDisplayPeriod = 0;

#ifdef ANDROID_ENABLED
	// our android wrapper will initialize our android loader at this point
	register_extension_wrapper(memnew(OpenXRAndroidExtension(this)));
#endif

	// register our other extensions
	register_extension_wrapper(memnew(OpenXRHTCViveTrackerExtension(this)));
}

OpenXRAPI::~OpenXRAPI() {
	// cleanup our composition layer providers
	for (OpenXRCompositionLayerProvider *provider : composition_layer_providers) {
		memdelete(provider);
	}
	composition_layer_providers.clear();

	// cleanup our extension wrappers
	for (OpenXRExtensionWrapper *extension_wrapper : registered_extension_wrappers) {
		memdelete(extension_wrapper);
	}
	registered_extension_wrappers.clear();

	if (supported_extensions != nullptr) {
		memfree(supported_extensions);
		supported_extensions = nullptr;
	}

	if (layer_properties != nullptr) {
		memfree(layer_properties);
		layer_properties = nullptr;
	}

	singleton = nullptr;
}

Transform3D OpenXRAPI::transform_from_pose(const XrPosef &p_pose) {
	Quaternion q(p_pose.orientation.x, p_pose.orientation.y, p_pose.orientation.z, p_pose.orientation.w);
	Basis basis(q);
	Vector3 origin(p_pose.position.x, p_pose.position.y, p_pose.position.z);

	return Transform3D(basis, origin);
}

template <typename T>
XRPose::TrackingConfidence _transform_from_location(const T &p_location, Transform3D &r_transform) {
	Basis basis;
	Vector3 origin;
	XRPose::TrackingConfidence confidence = XRPose::XR_TRACKING_CONFIDENCE_NONE;
	const auto &pose = p_location.pose;

	// Check orientation
	if (p_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
		Quaternion q(pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w);
		r_transform.basis = Basis(q);

		if (p_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) {
			// Fully valid orientation, so either 3DOF or 6DOF tracking with high confidence so default to HIGH_TRACKING
			confidence = XRPose::XR_TRACKING_CONFIDENCE_HIGH;
		} else {
			// Orientation is being tracked but we're using old/predicted data, so low tracking confidence
			confidence = XRPose::XR_TRACKING_CONFIDENCE_LOW;
		}
	} else {
		r_transform.basis = Basis();
	}

	// Check location
	if (p_location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) {
		r_transform.origin = Vector3(pose.position.x, pose.position.y, pose.position.z);

		if (!(p_location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)) {
			// Location is being tracked but we're using old/predicted data, so low tracking confidence
			confidence = XRPose::XR_TRACKING_CONFIDENCE_LOW;
		} else if (confidence == XRPose::XR_TRACKING_CONFIDENCE_NONE) {
			// Position tracking without orientation tracking?
			confidence = XRPose::XR_TRACKING_CONFIDENCE_HIGH;
		}
	} else {
		// No tracking or 3DOF I guess..
		r_transform.origin = Vector3();
	}

	return confidence;
}

XRPose::TrackingConfidence OpenXRAPI::transform_from_location(const XrSpaceLocation &p_location, Transform3D &r_transform) {
	return _transform_from_location(p_location, r_transform);
}

XRPose::TrackingConfidence OpenXRAPI::transform_from_location(const XrHandJointLocationEXT &p_location, Transform3D &r_transform) {
	return _transform_from_location(p_location, r_transform);
}

void OpenXRAPI::parse_velocities(const XrSpaceVelocity &p_velocity, Vector3 &r_linear_velocity, Vector3 &r_angular_velocity) {
	if (p_velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) {
		XrVector3f linear_velocity = p_velocity.linearVelocity;
		r_linear_velocity = Vector3(linear_velocity.x, linear_velocity.y, linear_velocity.z);
	} else {
		r_linear_velocity = Vector3();
	}
	if (p_velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) {
		XrVector3f angular_velocity = p_velocity.angularVelocity;
		r_angular_velocity = Vector3(angular_velocity.x, angular_velocity.y, angular_velocity.z);
	} else {
		r_angular_velocity = Vector3();
	}
}

RID OpenXRAPI::get_tracker_rid(XrPath p_path) {
	List<RID> current;
	tracker_owner.get_owned_list(&current);
	for (int i = 0; i < current.size(); i++) {
		Tracker *tracker = tracker_owner.get_or_null(current[i]);
		if (tracker && tracker->toplevel_path == p_path) {
			return current[i];
		}
	}

	return RID();
}

RID OpenXRAPI::tracker_create(const String p_name) {
	ERR_FAIL_COND_V(instance == XR_NULL_HANDLE, RID());

	Tracker new_tracker;
	new_tracker.name = p_name;
	new_tracker.toplevel_path = XR_NULL_PATH;
	new_tracker.active_profile_rid = RID();

	XrResult result = xrStringToPath(instance, p_name.utf8().get_data(), &new_tracker.toplevel_path);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to get path for ", p_name, "! [", get_error_string(result), "]");
		return RID();
	}

	return tracker_owner.make_rid(new_tracker);
}

String OpenXRAPI::tracker_get_name(RID p_tracker) {
	if (p_tracker.is_null()) {
		return String("None");
	}

	Tracker *tracker = tracker_owner.get_or_null(p_tracker);
	ERR_FAIL_NULL_V(tracker, String());

	return tracker->name;
}

void OpenXRAPI::tracker_check_profile(RID p_tracker, XrSession p_session) {
	if (p_session == XR_NULL_HANDLE) {
		p_session = session;
	}

	Tracker *tracker = tracker_owner.get_or_null(p_tracker);
	ERR_FAIL_NULL(tracker);

	if (tracker->toplevel_path == XR_NULL_PATH) {
		// no path, how was this even created?
		return;
	}

	XrInteractionProfileState profile_state = {
		XR_TYPE_INTERACTION_PROFILE_STATE, // type
		nullptr, // next
		XR_NULL_PATH // interactionProfile
	};

	XrResult result = xrGetCurrentInteractionProfile(p_session, tracker->toplevel_path, &profile_state);
	if (XR_FAILED(result)) {
		print_line("OpenXR: Failed to get interaction profile for", itos(tracker->toplevel_path), "[", get_error_string(result), "]");
		return;
	}

	XrPath new_profile = profile_state.interactionProfile;
	XrPath was_profile = get_interaction_profile_path(tracker->active_profile_rid);
	if (was_profile != new_profile) {
		tracker->active_profile_rid = get_interaction_profile_rid(new_profile);

		if (xr_interface) {
			xr_interface->tracker_profile_changed(p_tracker, tracker->active_profile_rid);
		}
	}
}

void OpenXRAPI::tracker_free(RID p_tracker) {
	Tracker *tracker = tracker_owner.get_or_null(p_tracker);
	ERR_FAIL_NULL(tracker);

	// there is nothing to free here

	tracker_owner.free(p_tracker);
}

RID OpenXRAPI::action_set_create(const String p_name, const String p_localized_name, const int p_priority) {
	ERR_FAIL_COND_V(instance == XR_NULL_HANDLE, RID());
	ActionSet action_set;

	action_set.name = p_name;
	action_set.is_attached = false;

	// create our action set...
	XrActionSetCreateInfo action_set_info = {
		XR_TYPE_ACTION_SET_CREATE_INFO, // type
		nullptr, // next
		"", // actionSetName
		"", // localizedActionSetName
		uint32_t(p_priority) // priority
	};

	copy_string_to_char_buffer(p_name, action_set_info.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE);
	copy_string_to_char_buffer(p_localized_name, action_set_info.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE);

	// print_line("Creating action set ", action_set_info.actionSetName, " - ", action_set_info.localizedActionSetName, " (", itos(action_set_info.priority), ")");

	XrResult result = xrCreateActionSet(instance, &action_set_info, &action_set.handle);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to create action set ", p_name, "! [", get_error_string(result), "]");
		return RID();
	}

	return action_set_owner.make_rid(action_set);
}

String OpenXRAPI::action_set_get_name(RID p_action_set) {
	if (p_action_set.is_null()) {
		return String("None");
	}

	ActionSet *action_set = action_set_owner.get_or_null(p_action_set);
	ERR_FAIL_NULL_V(action_set, String());

	return action_set->name;
}

bool OpenXRAPI::action_set_attach(RID p_action_set) {
	ActionSet *action_set = action_set_owner.get_or_null(p_action_set);
	ERR_FAIL_NULL_V(action_set, false);

	if (action_set->is_attached) {
		// already attached
		return true;
	}

	ERR_FAIL_COND_V(session == XR_NULL_HANDLE, false);

	// So according to the docs, once we attach our action set to our session it becomes read only..
	// https://www.khronos.org/registry/OpenXR/specs/1.0/man/html/xrAttachSessionActionSets.html
	XrSessionActionSetsAttachInfo attach_info = {
		XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, // type
		nullptr, // next
		1, // countActionSets,
		&action_set->handle // actionSets
	};

	XrResult result = xrAttachSessionActionSets(session, &attach_info);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to attach action set! [", get_error_string(result), "]");
		return false;
	}

	action_set->is_attached = true;

	/* For debugging:
	print_verbose("Attached set " + action_set->name);
	List<RID> action_rids;
	action_owner.get_owned_list(&action_rids);
	for (int i = 0; i < action_rids.size(); i++) {
		Action * action = action_owner.get_or_null(action_rids[i]);
		if (action && action->action_set_rid == p_action_set) {
			print_verbose(" - Action " + action->name + ": " + OpenXRUtil::get_action_type_name(action->action_type));
			for (int j = 0; j < action->trackers.size(); j++) {
				Tracker * tracker = tracker_owner.get_or_null(action->trackers[j].tracker_rid);
				if (tracker) {
					print_verbose("    - " + tracker->name);
				}
			}
		}
	}
	*/

	return true;
}

void OpenXRAPI::action_set_free(RID p_action_set) {
	ActionSet *action_set = action_set_owner.get_or_null(p_action_set);
	ERR_FAIL_NULL(action_set);

	if (action_set->handle != XR_NULL_HANDLE) {
		xrDestroyActionSet(action_set->handle);
	}

	action_set_owner.free(p_action_set);
}

RID OpenXRAPI::get_action_rid(XrAction p_action) {
	List<RID> current;
	action_owner.get_owned_list(&current);
	for (int i = 0; i < current.size(); i++) {
		Action *action = action_owner.get_or_null(current[i]);
		if (action && action->handle == p_action) {
			return current[i];
		}
	}

	return RID();
}

RID OpenXRAPI::action_create(RID p_action_set, const String p_name, const String p_localized_name, OpenXRAction::ActionType p_action_type, const Vector<RID> &p_trackers) {
	ERR_FAIL_COND_V(instance == XR_NULL_HANDLE, RID());

	Action action;
	action.name = p_name;

	ActionSet *action_set = action_set_owner.get_or_null(p_action_set);
	ERR_FAIL_NULL_V(action_set, RID());
	ERR_FAIL_COND_V(action_set->handle == XR_NULL_HANDLE, RID());
	action.action_set_rid = p_action_set;

	switch (p_action_type) {
		case OpenXRAction::OPENXR_ACTION_BOOL:
			action.action_type = XR_ACTION_TYPE_BOOLEAN_INPUT;
			break;
		case OpenXRAction::OPENXR_ACTION_FLOAT:
			action.action_type = XR_ACTION_TYPE_FLOAT_INPUT;
			break;
		case OpenXRAction::OPENXR_ACTION_VECTOR2:
			action.action_type = XR_ACTION_TYPE_VECTOR2F_INPUT;
			break;
		case OpenXRAction::OPENXR_ACTION_POSE:
			action.action_type = XR_ACTION_TYPE_POSE_INPUT;
			break;
		case OpenXRAction::OPENXR_ACTION_HAPTIC:
			action.action_type = XR_ACTION_TYPE_VIBRATION_OUTPUT;
			break;
		default:
			ERR_FAIL_V(RID());
			break;
	}

	Vector<XrPath> toplevel_paths;
	for (int i = 0; i < p_trackers.size(); i++) {
		Tracker *tracker = tracker_owner.get_or_null(p_trackers[i]);
		if (tracker != nullptr && tracker->toplevel_path != XR_NULL_PATH) {
			ActionTracker action_tracker = {
				p_trackers[i], // tracker
				XR_NULL_HANDLE, // space
				false // was_location_valid
			};
			action.trackers.push_back(action_tracker);

			toplevel_paths.push_back(tracker->toplevel_path);
		}
	}

	XrActionCreateInfo action_info = {
		XR_TYPE_ACTION_CREATE_INFO, // type
		nullptr, // next
		"", // actionName
		action.action_type, // actionType
		uint32_t(toplevel_paths.size()), // countSubactionPaths
		toplevel_paths.ptr(), // subactionPaths
		"" // localizedActionName
	};

	copy_string_to_char_buffer(p_name, action_info.actionName, XR_MAX_ACTION_NAME_SIZE);
	copy_string_to_char_buffer(p_localized_name, action_info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE);

	// print_line("Creating action ", action_info.actionName, action_info.localizedActionName, action_info.countSubactionPaths);

	XrResult result = xrCreateAction(action_set->handle, &action_info, &action.handle);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to create action ", p_name, "! [", get_error_string(result), "]");
		return RID();
	}

	return action_owner.make_rid(action);
}

String OpenXRAPI::action_get_name(RID p_action) {
	if (p_action.is_null()) {
		return String("None");
	}

	Action *action = action_owner.get_or_null(p_action);
	ERR_FAIL_NULL_V(action, String());

	return action->name;
}

void OpenXRAPI::action_free(RID p_action) {
	Action *action = action_owner.get_or_null(p_action);
	ERR_FAIL_NULL(action);

	if (action->handle != XR_NULL_HANDLE) {
		xrDestroyAction(action->handle);
	}

	action_owner.free(p_action);
}

RID OpenXRAPI::get_interaction_profile_rid(XrPath p_path) {
	List<RID> current;
	interaction_profile_owner.get_owned_list(&current);
	for (int i = 0; i < current.size(); i++) {
		InteractionProfile *ip = interaction_profile_owner.get_or_null(current[i]);
		if (ip && ip->path == p_path) {
			return current[i];
		}
	}

	return RID();
}

XrPath OpenXRAPI::get_interaction_profile_path(RID p_interaction_profile) {
	if (p_interaction_profile.is_null()) {
		return XR_NULL_PATH;
	}

	InteractionProfile *ip = interaction_profile_owner.get_or_null(p_interaction_profile);
	ERR_FAIL_NULL_V(ip, XR_NULL_PATH);

	return ip->path;
}

RID OpenXRAPI::interaction_profile_create(const String p_name) {
	InteractionProfile new_interaction_profile;

	XrResult result = xrStringToPath(instance, p_name.utf8().get_data(), &new_interaction_profile.path);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to get path for ", p_name, "! [", get_error_string(result), "]");
		return RID();
	}

	RID existing_ip = get_interaction_profile_rid(new_interaction_profile.path);
	if (existing_ip.is_valid()) {
		return existing_ip;
	}

	new_interaction_profile.name = p_name;
	return interaction_profile_owner.make_rid(new_interaction_profile);
}

String OpenXRAPI::interaction_profile_get_name(RID p_interaction_profile) {
	if (p_interaction_profile.is_null()) {
		return String("None");
	}

	InteractionProfile *ip = interaction_profile_owner.get_or_null(p_interaction_profile);
	ERR_FAIL_NULL_V(ip, String());

	return ip->name;
}

void OpenXRAPI::interaction_profile_clear_bindings(RID p_interaction_profile) {
	InteractionProfile *ip = interaction_profile_owner.get_or_null(p_interaction_profile);
	ERR_FAIL_NULL(ip);

	ip->bindings.clear();
}

bool OpenXRAPI::interaction_profile_add_binding(RID p_interaction_profile, RID p_action, const String p_path) {
	InteractionProfile *ip = interaction_profile_owner.get_or_null(p_interaction_profile);
	ERR_FAIL_NULL_V(ip, false);

	XrActionSuggestedBinding binding;

	Action *action = action_owner.get_or_null(p_action);
	ERR_FAIL_COND_V(action == nullptr || action->handle == XR_NULL_HANDLE, false);

	binding.action = action->handle;

	XrResult result = xrStringToPath(instance, p_path.utf8().get_data(), &binding.binding);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to get path for ", p_path, "! [", get_error_string(result), "]");
		return false;
	}

	ip->bindings.push_back(binding);

	return true;
}

bool OpenXRAPI::interaction_profile_suggest_bindings(RID p_interaction_profile) {
	ERR_FAIL_COND_V(instance == XR_NULL_HANDLE, false);

	InteractionProfile *ip = interaction_profile_owner.get_or_null(p_interaction_profile);
	ERR_FAIL_NULL_V(ip, false);

	const XrInteractionProfileSuggestedBinding suggested_bindings = {
		XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING, // type
		nullptr, // next
		ip->path, // interactionProfile
		uint32_t(ip->bindings.size()), // countSuggestedBindings
		ip->bindings.ptr() // suggestedBindings
	};

	XrResult result = xrSuggestInteractionProfileBindings(instance, &suggested_bindings);
	if (result == XR_ERROR_PATH_UNSUPPORTED) {
		// this is fine, not all runtimes support all devices.
		print_verbose("OpenXR Interaction profile " + ip->name + " is not supported on this runtime");
	} else if (XR_FAILED(result)) {
		print_line("OpenXR: failed to suggest bindings for ", ip->name, "! [", get_error_string(result), "]");
		// reporting is enough...
	}

	/* For debugging:
	print_verbose("Suggested bindings for " + ip->name);
	for (int i = 0; i < ip->bindings.size(); i++) {
		uint32_t strlen;
		char path[XR_MAX_PATH_LENGTH];

		String action_name = action_get_name(get_action_rid(ip->bindings[i].action));

		XrResult result = xrPathToString(instance, ip->bindings[i].binding, XR_MAX_PATH_LENGTH, &strlen, path);
		if (XR_FAILED(result)) {
			print_line("OpenXR: failed to retrieve bindings for ", action_name, "! [", get_error_string(result), "]");
		}
		print_verbose(" - " + action_name + " => " + String(path));
	}
	*/

	return true;
}

void OpenXRAPI::interaction_profile_free(RID p_interaction_profile) {
	InteractionProfile *ip = interaction_profile_owner.get_or_null(p_interaction_profile);
	ERR_FAIL_NULL(ip);

	ip->bindings.clear();

	interaction_profile_owner.free(p_interaction_profile);
}

bool OpenXRAPI::sync_action_sets(const Vector<RID> p_active_sets) {
	ERR_FAIL_COND_V(session == XR_NULL_HANDLE, false);

	if (!running) {
		return false;
	}

	Vector<XrActiveActionSet> active_sets;
	for (int i = 0; i < p_active_sets.size(); i++) {
		ActionSet *action_set = action_set_owner.get_or_null(p_active_sets[i]);
		if (action_set && action_set->handle != XR_NULL_HANDLE) {
			XrActiveActionSet aset;
			aset.actionSet = action_set->handle;
			aset.subactionPath = XR_NULL_PATH;
			active_sets.push_back(aset);
		}
	}

	ERR_FAIL_COND_V(active_sets.size() == 0, false);

	XrActionsSyncInfo sync_info = {
		XR_TYPE_ACTIONS_SYNC_INFO, // type
		nullptr, // next
		uint32_t(active_sets.size()), // countActiveActionSets
		active_sets.ptr() // activeActionSets
	};

	XrResult result = xrSyncActions(session, &sync_info);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to sync active action sets! [", get_error_string(result), "]");
		return false;
	}

	return true;
}

bool OpenXRAPI::get_action_bool(RID p_action, RID p_tracker) {
	ERR_FAIL_COND_V(session == XR_NULL_HANDLE, false);
	Action *action = action_owner.get_or_null(p_action);
	ERR_FAIL_NULL_V(action, false);
	Tracker *tracker = tracker_owner.get_or_null(p_tracker);
	ERR_FAIL_NULL_V(tracker, false);

	if (!running) {
		return false;
	}

	ERR_FAIL_COND_V(action->action_type != XR_ACTION_TYPE_BOOLEAN_INPUT, false);

	XrActionStateGetInfo get_info = {
		XR_TYPE_ACTION_STATE_GET_INFO, // type
		nullptr, // next
		action->handle, // action
		tracker->toplevel_path // subactionPath
	};

	XrActionStateBoolean result_state;
	result_state.type = XR_TYPE_ACTION_STATE_BOOLEAN,
	result_state.next = nullptr;
	XrResult result = xrGetActionStateBoolean(session, &get_info, &result_state);
	if (XR_FAILED(result)) {
		print_line("OpenXR: couldn't get action boolean! [", get_error_string(result), "]");
		return false;
	}

	return result_state.isActive && result_state.currentState;
}

float OpenXRAPI::get_action_float(RID p_action, RID p_tracker) {
	ERR_FAIL_COND_V(session == XR_NULL_HANDLE, 0.0);
	Action *action = action_owner.get_or_null(p_action);
	ERR_FAIL_NULL_V(action, 0.0);
	Tracker *tracker = tracker_owner.get_or_null(p_tracker);
	ERR_FAIL_NULL_V(tracker, 0.0);

	if (!running) {
		return 0.0;
	}

	ERR_FAIL_COND_V(action->action_type != XR_ACTION_TYPE_FLOAT_INPUT, 0.0);

	XrActionStateGetInfo get_info = {
		XR_TYPE_ACTION_STATE_GET_INFO, // type
		nullptr, // next
		action->handle, // action
		tracker->toplevel_path // subactionPath
	};

	XrActionStateFloat result_state;
	result_state.type = XR_TYPE_ACTION_STATE_FLOAT,
	result_state.next = nullptr;
	XrResult result = xrGetActionStateFloat(session, &get_info, &result_state);
	if (XR_FAILED(result)) {
		print_line("OpenXR: couldn't get action float! [", get_error_string(result), "]");
		return 0.0;
	}

	return result_state.isActive ? result_state.currentState : 0.0;
}

Vector2 OpenXRAPI::get_action_vector2(RID p_action, RID p_tracker) {
	ERR_FAIL_COND_V(session == XR_NULL_HANDLE, Vector2());
	Action *action = action_owner.get_or_null(p_action);
	ERR_FAIL_NULL_V(action, Vector2());
	Tracker *tracker = tracker_owner.get_or_null(p_tracker);
	ERR_FAIL_NULL_V(tracker, Vector2());

	if (!running) {
		return Vector2();
	}

	ERR_FAIL_COND_V(action->action_type != XR_ACTION_TYPE_VECTOR2F_INPUT, Vector2());

	XrActionStateGetInfo get_info = {
		XR_TYPE_ACTION_STATE_GET_INFO, // type
		nullptr, // next
		action->handle, // action
		tracker->toplevel_path // subactionPath
	};

	XrActionStateVector2f result_state;
	result_state.type = XR_TYPE_ACTION_STATE_VECTOR2F,
	result_state.next = nullptr;
	XrResult result = xrGetActionStateVector2f(session, &get_info, &result_state);
	if (XR_FAILED(result)) {
		print_line("OpenXR: couldn't get action vector2! [", get_error_string(result), "]");
		return Vector2();
	}

	return result_state.isActive ? Vector2(result_state.currentState.x, result_state.currentState.y) : Vector2();
}

XRPose::TrackingConfidence OpenXRAPI::get_action_pose(RID p_action, RID p_tracker, Transform3D &r_transform, Vector3 &r_linear_velocity, Vector3 &r_angular_velocity) {
	ERR_FAIL_COND_V(session == XR_NULL_HANDLE, XRPose::XR_TRACKING_CONFIDENCE_NONE);
	Action *action = action_owner.get_or_null(p_action);
	ERR_FAIL_NULL_V(action, XRPose::XR_TRACKING_CONFIDENCE_NONE);
	Tracker *tracker = tracker_owner.get_or_null(p_tracker);
	ERR_FAIL_NULL_V(tracker, XRPose::XR_TRACKING_CONFIDENCE_NONE);

	if (!running) {
		return XRPose::XR_TRACKING_CONFIDENCE_NONE;
	}

	ERR_FAIL_COND_V(action->action_type != XR_ACTION_TYPE_POSE_INPUT, XRPose::XR_TRACKING_CONFIDENCE_NONE);

	// print_verbose("Checking " + action->name + " => " + tracker->name + " (" + itos(tracker->toplevel_path) + ")");

	uint64_t index = 0xFFFFFFFF;
	uint64_t size = uint64_t(action->trackers.size());
	for (uint64_t i = 0; i < size && index == 0xFFFFFFFF; i++) {
		if (action->trackers[i].tracker_rid == p_tracker) {
			index = i;
		}
	}

	if (index == 0xFFFFFFFF) {
		// couldn't find it?
		return XRPose::XR_TRACKING_CONFIDENCE_NONE;
	}

	XrTime display_time = get_next_frame_time();
	if (display_time == 0) {
		return XRPose::XR_TRACKING_CONFIDENCE_NONE;
	}

	if (action->trackers[index].space == XR_NULL_HANDLE) {
		// if this is a pose we need to define spaces

		XrActionSpaceCreateInfo action_space_info = {
			XR_TYPE_ACTION_SPACE_CREATE_INFO, // type
			nullptr, // next
			action->handle, // action
			tracker->toplevel_path, // subactionPath
			{
					{ 0.0, 0.0, 0.0, 1.0 }, // orientation
					{ 0.0, 0.0, 0.0 } // position
			} // poseInActionSpace
		};

		XrSpace space;
		XrResult result = xrCreateActionSpace(session, &action_space_info, &space);
		if (XR_FAILED(result)) {
			print_line("OpenXR: couldn't create action space! [", get_error_string(result), "]");
			return XRPose::XR_TRACKING_CONFIDENCE_NONE;
		}

		action->trackers.ptrw()[index].space = space;
	}

	XrSpaceVelocity velocity = {
		XR_TYPE_SPACE_VELOCITY, // type
		nullptr, // next
		0, // velocityFlags
		{ 0.0, 0.0, 0.0 }, // linearVelocity
		{ 0.0, 0.0, 0.0 } // angularVelocity
	};

	XrSpaceLocation location = {
		XR_TYPE_SPACE_LOCATION, // type
		&velocity, // next
		0, // locationFlags
		{
				{ 0.0, 0.0, 0.0, 0.0 }, // orientation
				{ 0.0, 0.0, 0.0 } // position
		} // pose
	};

	XrResult result = xrLocateSpace(action->trackers[index].space, play_space, display_time, &location);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to locate space! [", get_error_string(result), "]");
		return XRPose::XR_TRACKING_CONFIDENCE_NONE;
	}

	XRPose::TrackingConfidence confidence = transform_from_location(location, r_transform);
	parse_velocities(velocity, r_linear_velocity, r_angular_velocity);

	return confidence;
}

bool OpenXRAPI::trigger_haptic_pulse(RID p_action, RID p_tracker, float p_frequency, float p_amplitude, XrDuration p_duration_ns) {
	ERR_FAIL_COND_V(session == XR_NULL_HANDLE, false);
	Action *action = action_owner.get_or_null(p_action);
	ERR_FAIL_NULL_V(action, false);
	Tracker *tracker = tracker_owner.get_or_null(p_tracker);
	ERR_FAIL_NULL_V(tracker, false);

	if (!running) {
		return false;
	}

	ERR_FAIL_COND_V(action->action_type != XR_ACTION_TYPE_VIBRATION_OUTPUT, false);

	XrHapticActionInfo action_info = {
		XR_TYPE_HAPTIC_ACTION_INFO, // type
		nullptr, // next
		action->handle, // action
		tracker->toplevel_path // subactionPath
	};

	XrHapticVibration vibration = {
		XR_TYPE_HAPTIC_VIBRATION, // type
		nullptr, // next
		p_duration_ns, // duration
		p_frequency, // frequency
		p_amplitude, // amplitude
	};

	XrResult result = xrApplyHapticFeedback(session, &action_info, (const XrHapticBaseHeader *)&vibration);
	if (XR_FAILED(result)) {
		print_line("OpenXR: failed to apply haptic feedback! [", get_error_string(result), "]");
		return false;
	}

	return true;
}
