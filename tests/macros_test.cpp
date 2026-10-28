// The macros module on its own (no header, no root module): the constants and
// the compatibility names are constant expressions, as the macros are, so they
// work in constant-initialised tables, array bounds and static_asserts.
import std;
import mcpplibs.mwrap.miniaudio.macros;

using namespace mcpplibs::mwrap::miniaudio::macros;

// Every constant has the type its macro expands to.
static_assert(std::is_same_v<decltype(MA_VERSION_MAJOR), const int>);
static_assert(std::is_same_v<decltype(MA_MAX_CHANNELS), const int>);                 // 254
// MA_SIZE_MAX is SIZE_MAX where the header sees <stdint.h>, and 0xFFFFFFFF
// where it does not: the module includes <miniaudio.h> alone.
static_assert(std::is_same_v<decltype(MA_SIZE_MAX), const std::size_t> || std::is_same_v<decltype(MA_SIZE_MAX), const unsigned int>);
static_assert(std::is_same_v<decltype(MA_UINT64_MAX), const unsigned long long>      // ma_uint64
              || std::is_same_v<decltype(MA_UINT64_MAX), const unsigned long>);
static_assert(std::is_same_v<decltype(MA_LISTENER_INDEX_CLOSEST), const unsigned char>);  // ((ma_uint8)-1)
static_assert(std::is_same_v<decltype(MA_DATA_FORMAT_FLAG_EXCLUSIVE_MODE), const unsigned int>);  // (1U << 1)
static_assert(std::is_same_v<decltype(MA_SOUND_SOURCE_CHANNEL_COUNT), const unsigned int>);  // 0xFFFFFFFF
static_assert(std::is_same_v<decltype(MA_VERSION_STRING), const char (&)[8]>);

static_assert(MA_VERSION_MAJOR == 0 && MA_VERSION_MINOR == 11 && MA_VERSION_REVISION == 25);
static_assert(std::string_view(MA_VERSION_STRING) == "0.11.25");
static_assert(MA_TRUE == 1 && MA_FALSE == 0);
static_assert(MA_MIN_CHANNELS == 1 && MA_MAX_CHANNELS == 254 && MA_MAX_FILTER_ORDER == 8);
static_assert(MA_SIZE_MAX == std::numeric_limits<std::size_t>::max() || MA_SIZE_MAX == 0xFFFFFFFFu);
static_assert(MA_UINT64_MAX == std::numeric_limits<std::uint64_t>::max());
static_assert(MA_SIZEOF_PTR == sizeof(void*));
static_assert(MA_SIMD_ALIGNMENT == 32 && MA_MAX_LOG_CALLBACKS == 4 && MA_CHANNEL_INDEX_NULL == 255);
static_assert(MA_MAX_NODE_BUS_COUNT == 254 && MA_MAX_NODE_LOCAL_BUS_COUNT == 2 && MA_NODE_BUS_COUNT_UNKNOWN == 255);
static_assert(MA_ENGINE_MAX_LISTENERS == 4 && MA_LISTENER_INDEX_CLOSEST == 255);
static_assert(MA_RESOURCE_MANAGER_MAX_JOB_THREAD_COUNT == 64 && MA_MAX_DEVICE_NAME_LENGTH == 255);
static_assert(MA_DATA_FORMAT_FLAG_EXCLUSIVE_MODE == 2u && MA_SOUND_SOURCE_CHANNEL_COUNT == 0xFFFFFFFFu);
static_assert(MA_DATA_SOURCE_SELF_MANAGED_RANGE_AND_LOOP_POINT == 1);
static_assert(MA_BACKEND_COUNT > 10);  // ma_backend_null + 1

// An array bound and a table, at compile time.
constexpr std::array<int, MA_ENGINE_MAX_LISTENERS> listeners{};
static_assert(listeners.size() == 4);
char name[MA_MAX_DEVICE_NAME_LENGTH + 1];

// The compatibility names are the job queue's.
static_assert(std::is_class_v<ma_resource_manager_job> && std::is_class_v<ma_resource_manager_job_queue>);
static_assert(sizeof(ma_resource_manager_job_queue_config) > 0);
static_assert(std::is_function_v<std::remove_reference_t<decltype(ma_resource_manager_job_queue_init)>>);

int main() {
    name[0] = 0;
    return listeners[0] + name[0];
}
