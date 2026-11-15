#pragma once

#include <exception>
#include <functional>
#include <future>
#include <type_traits>
#include <utility>

// İş parçacığı olmayan derlemeler (Web oyuncusu, Player Settings > Web > Threads kapalı: SharedArrayBuffer'sız her sunucuda açılır):
// std::thread / std::async(std::launch::async) orada hata fırlatır. Motor bu başlıkla sorar; arka plan işleri çağıran yerde hemen
// çalışır (sonuç hazır future döner, "hazır mı" yoklaması aynı kalır).
namespace Conqueror::Threads
{
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    inline constexpr bool Available = false;
#else
    inline constexpr bool Available = true;
#endif

    // std::async(std::launch::async, f, args...) yerine
    template<typename F, typename... Args>
    auto Async(F&& f, Args&&... args) -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>
    {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
        if constexpr (Available)
        {
            return std::async(std::launch::async, std::forward<F>(f), std::forward<Args>(args)...);
        }
        else
        {
            std::promise<R> promise;
            try
            {
                if constexpr (std::is_void_v<R>)
                {
                    std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
                    promise.set_value();
                }
                else
                    promise.set_value(std::invoke(std::forward<F>(f), std::forward<Args>(args)...));
            }
            catch (...)
            {
                promise.set_exception(std::current_exception());
            }
            return promise.get_future();
        }
    }
}
