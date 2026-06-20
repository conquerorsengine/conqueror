#pragma once

// Conqueror Engine Public API
// Kullanıcı script'leri için tek include point

// Core
#include "../../src/Core/Base/Base.h"
#include "../../src/Core/KeyCodes.h"
#include "../../src/Core/MouseCodes.h"
#include "../../src/Core/Input/Input.h"
#include "../../src/Core/Time/Timestep.h"

// Audio
#include "../../src/Core/Audio/AudioEngine.h"
#include "../../src/Core/Audio/AudioSource.h"

// Physics
#include "../../src/Core/PhysicsSystem/Physics.h"

// Scene & Entity
#include "../../src/Scene/Entity.h"
#include "../../src/Scene/Components.h"
#include "../../src/Scene/Scene.h"

// Scripting
#include "../../src/Scripting/ScriptableEntity.h"

// Math (GLM)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Math & Debug
#include "../../src/Core/Math/Math.h"
#include "../../src/Core/Math/Random.h"
#include "../../src/Core/Math/Curve.h"
#include "../../src/Core/Debug/DebugDraw.h"
#include "../../src/Core/Debug/DebugPalette.h"
#include "../../src/Core/Debug/DebugSettings.h"

// Logging
#include "../../src/Core/Logging/Log.h"

// Utils
#include "../../src/Core/Utils/UUID.h"
#include "../../src/Core/Utils/FileSystem.h"
#include "../../src/Core/Utils/Profiler.h"
#include "../../src/Core/Utils/ThreadPool.h"
#include "../../src/Core/Utils/CommandLine.h"
#include "../../src/Core/Utils/Compression.h"
#include "../../src/Core/Utils/Platform.h"

// Events
#include "../../src/Core/Events/Event.h"
#include "../../src/Core/Events/ApplicationEvent.h"
#include "../../src/Core/Events/KeyEvent.h"
#include "../../src/Core/Events/MouseEvent.h"
#include "../../src/Core/Events/EventBus.h"
