#include <windows.h>

#include <sdk/UObjectArray.hpp>
#include <sdk/UObjectBase.hpp>

#include "FUObjectArrayFunctions.hpp"


namespace uevr {
namespace fuobjectarray {
UEVR_UObjectHandle find_uobject(const wchar_t* name) {
    return (UEVR_UObjectHandle)sdk::find_uobject(name);
}

bool is_chunked() { 
    return sdk::FUObjectArray::is_chunked();
}

bool is_inlined() {
    return sdk::FUObjectArray::is_inlined();
}

unsigned int get_objects_offset() {
    return sdk::FUObjectArray::get_objects_offset();
}

unsigned int get_item_distance() {
    return sdk::FUObjectArray::get_item_distance();
}

int get_object_count(UEVR_UObjectArrayHandle array) {
    return ((sdk::FUObjectArray*)array)->get_object_count();
}

void* get_objects_ptr(UEVR_UObjectArrayHandle array) {
    return ((sdk::FUObjectArray*)array)->get_objects_ptr();
}

UEVR_UObjectHandle get_object(UEVR_UObjectArrayHandle array, int index) {
    auto item = ((sdk::FUObjectArray*)array)->get_object(index);

    if (item == nullptr) {
        return nullptr;
    }

    return (UEVR_UObjectHandle)item->object;
}

// messed up naming, I know
UEVR_FUObjectItemHandle get_item(UEVR_UObjectArrayHandle array, int index) {
    return (UEVR_FUObjectItemHandle)((sdk::FUObjectArray*)array)->get_object(index);
}

namespace {
// The one read that may touch freed memory, apart: __try cannot share a function with objects that unwind.
bool read_internal_index(const sdk::UObjectBase* object, int32_t* index) {
    __try {
        *index = (int32_t)object->get_internal_index();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}

// A live object is the one its own slot holds: a freed object's slot is cleared or given to another, so a pointer kept
// past its object's end fails whatever its memory holds now. A new object at the old address passes; only its class tells.
bool is_valid_object(UEVR_UObjectArrayHandle array, UEVR_UObjectHandle handle) {
    const auto objects = (sdk::FUObjectArray*)array;
    const auto object = (sdk::UObjectBase*)handle;

    if (objects == nullptr || object == nullptr) {
        return false;
    }

    int32_t index{-1};

    if (!read_internal_index(object, &index) || index < 0 || index >= objects->get_object_count()) {
        return false;
    }

    const auto item = objects->get_object(index);

    return item != nullptr && item->object == object;
}

UEVR_UObjectArrayFunctions functions {
    .find_uobject = uevr::fuobjectarray::find_uobject,
    .is_chunked = uevr::fuobjectarray::is_chunked,
    .is_inlined = uevr::fuobjectarray::is_inlined,
    .get_objects_offset = uevr::fuobjectarray::get_objects_offset,
    .get_item_distance = uevr::fuobjectarray::get_item_distance,
    .get_object_count = uevr::fuobjectarray::get_object_count,
    .get_objects_ptr = uevr::fuobjectarray::get_objects_ptr,
    .get_object = uevr::fuobjectarray::get_object,
    .get_item = uevr::fuobjectarray::get_item,
    .is_valid_object = uevr::fuobjectarray::is_valid_object
};
}
}