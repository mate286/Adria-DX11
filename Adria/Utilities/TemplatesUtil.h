#pragma once
#include <type_traits>
#include <utility>

namespace adria
{

	template <typename ... Trest>
	struct unique_types;

	template <typename T1, typename T2, typename ... Trest>
	struct unique_types<T1, T2, Trest ...>
		: unique_types<T1, T2>, unique_types<T1, Trest ...>, unique_types<T2, Trest ...> {};

	template <typename T1, typename T2>
	struct unique_types<T1, T2>
	{
		static_assert(!std::is_same<T1, T2>::value, "Types must be unique");
	};


	template<typename Base, typename Derived>
	struct is_derived_from : std::bool_constant<std::is_base_of_v<Base, Derived>&&
		std::is_convertible_v<const volatile Derived*, const volatile Base*>>
	{};

	template<typename Base, typename Derived>
	inline constexpr Bool is_derived_from_v = is_derived_from<Base, Derived>::value;


	template <typename T, typename... Ts>
	constexpr Bool Contains = (std::is_same<T, Ts>{} || ...);
	template <typename Subset, typename Set>
	constexpr Bool IsSubsetOf = false;
	template <typename... Ts, typename... Us>
	constexpr Bool IsSubsetOf<std::tuple<Ts...>, std::tuple<Us...>>
		= (Contains<Ts, Us...> && ...);



//#define DECLARE_HAS_MEMBER_STRUCT(name) \
//template <typename T, typename U = int> \
//struct has_##name : std::false_type { }; \
//template <typename T> \
//struct has_##name <T, decltype(std::declval<T>().name, 0)> : std::true_type { }; \
//template<typename T> constexpr bool has_##name##_v =  has_##name <T>::value
//
//DECLARE_HAS_MEMBER_STRUCT(position);
//DECLARE_HAS_MEMBER_STRUCT(normal);

}