#include "PCH.h"

#include <chrono>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace
{
	// ============================================================
	// Build / Version
	// ============================================================

	inline constexpr std::string_view kInternalVersion{ "1.0.0" };

	// SE melee physical mutation.
	// AE pre-damage physical path remains unverified; AE keeps mutation disabled.
	inline constexpr bool kEnablePhysicalMutation = true;


	// ============================================================
	// Gameplay - Melee
	// ============================================================

	inline constexpr float kNormalCostRate = 0.05F;
	inline constexpr float kPowerCostRate = 0.10F;

	inline constexpr float kOneHandDamageMult = 0.80F;
	inline constexpr float kTwoHandDamageMult = 1.00F;

	inline constexpr float kNormalDamageMult = 1.00F;
	inline constexpr float kPowerDamageMult = 2.40F;


	// ============================================================
	// Gameplay - Ranged Design
	//
	// v1.0.0 ranged core: dual-source, fire-grouped and thread-safe.
	//
	// Primary fire source:
	//   TESPlayerBowShotEvent -> one FireShotState.
	//
	// Fallback fire source:
	//   A real conversion ArrowProjectile with no compatible BowShot event
	//   creates a projectile-authoritative synthetic FireShotState.
	//
	// - 12% prepay occurs exactly once per FireShotState.
	// - simultaneous sibling projectiles share one FireShotState.
	// - fast full-auto projectiles without BowShotEvent become independent fires.
	// - first valid Actor impact refunds 8% once per fire.
	// - each valid Actor impact may receive additive Magic Shot damage.
	// - projectile poison removal remains projectile-local.
	// - projectile Power is intentionally NOT used for grouping.
	// - first-seen and impact paths are serialized.
	// ============================================================

	inline constexpr float kRangedDamageMult = 1.00F;

	inline constexpr float kRangedPrepayRate = 0.12F;
	inline constexpr float kRangedRefundRate = 0.08F;

	inline constexpr auto kFireBindWindow =
		std::chrono::milliseconds{ 1500 };

	// After the first projectile is bound, only projectiles appearing
	// inside this tight window may join the same fire state.
	//
	// The current 3-projectile test bow produces its projectiles in the
	// same millisecond, while the tested full-auto rifle produces rounds
	// tens of milliseconds apart. 25 ms separates those two patterns.
	inline constexpr auto kMultiProjectileJoinWindow =
		std::chrono::milliseconds{ 25 };

	inline constexpr std::size_t kRecentFireShotLimit = 128;

	inline constexpr auto kBindingPruneAge =
		std::chrono::seconds{ 120 };

	inline constexpr std::size_t kBindingPruneThreshold = 4096;


	// ============================================================
	// ESP
	// ============================================================

	constexpr std::string_view kPluginFileName{
		"[TULL] Magicka Blade.esp"
	};

	constexpr RE::FormID kConversionKeywordLocalID{
		0x00000D6E
	};

	constexpr RE::FormID kBoundConversionEffectLocalID{
		0x00000D6D
	};


	// ============================================================
	// HitData hand helper
	// ============================================================

	inline constexpr std::uint32_t kHitDataOffhandMask =
		1u << 17;


	// ============================================================
	// Types
	// ============================================================

	enum class HitHand
	{
		kUnknown,
		kRight,
		kLeft,
		kBoth
	};


	enum class WeaponDamageClass
	{
		kUnsupported,
		kOneHanded,
		kTwoHanded,
		kRanged
	};


	struct HitWeaponContext
	{
		RE::InventoryEntryData* entry{
			nullptr
		};

		HitHand hand{
			HitHand::kUnknown
		};


		bool attackDataLeft{
			false
		};

		bool offhandFlag{
			false
		};

	};


	struct ConversionInfo
	{
		float baseConversionRate{
			0.0F
		};

		RE::EffectSetting* conversionEffect{
			nullptr
		};

		bool scaleWithConjuration{
			false
		};
	};


	struct ActorValueSnapshot
	{
		float current{
			0.0F
		};

		float maximum{
			0.0F
		};

		bool valid{
			false
		};
	};


	struct TransactionCalculation
	{
		float cost{ 0.0F };
		float damage{ 0.0F };
		bool valid{ false };
	};


	struct RangedAmmoSnapshot
	{
		RE::TESAmmo* ammo{ nullptr };
		RE::BGSProjectile* projectileBase{ nullptr };
	};

	enum class RangedFireSource
	{
		kBowShotEvent,
		kProjectileFallback
	};


	struct FireShotState
	{
		mutable std::mutex mutex;

		RangedFireSource fireSource{ RangedFireSource::kBowShotEvent };
		RE::FormID weapon{ 0 };
		RE::FormID ammo{ 0 };
		RE::FormID projectileBase{ 0 };

		float prepayCost{ 0.0F };
		float refundCost{ 0.0F };
		float magicDamagePerHit{ 0.0F };

		std::uint32_t projectileCount{ 0 };

		bool approved{ false };
		bool paid{ false };
		bool refunded{ false };

		std::chrono::steady_clock::time_point createdAt{};
		std::chrono::steady_clock::time_point firstProjectileAt{};
	};


	struct ProjectileBinding
	{
		std::uintptr_t projectilePtr{ 0 };
		std::chrono::steady_clock::time_point firstSeenAt{};
		std::shared_ptr<FireShotState> shot;
	};

	// ============================================================
	// Runtime state
	// ============================================================

	RE::BGSKeyword* g_conversionKeyword{
		nullptr
	};

	RE::EffectSetting* g_boundConversionEffect{
		nullptr
	};


	std::unordered_map<
		RE::EnchantmentItem*,
		std::optional<ConversionInfo>>
		g_enchantmentCache;

	std::mutex g_enchantmentCacheMutex;


	bool g_hitHookInstalled{
		false
	};

	bool g_poisonSinkRegistered{
		false
	};


	bool g_playerBowShotSinkRegistered{
		false
	};


	bool g_arrowProjectileHookInstalled{
		false
	};


	// Shared ranged state.
	// g_rangedStateMutex protects observed keys, recent fire states, and
	// projectile -> FireShotState bindings.
	std::mutex g_rangedStateMutex;

	// Serializes authoritative fire creation:
	// TESPlayerBowShotEvent creation vs projectile-authoritative fallback.
	// This prevents a projectile from creating a synthetic shot while its
	// BowShotEvent is still in the middle of creating the real shot.
	std::mutex g_fireCreationMutex;

	// Serialize first-seen and impact paths. These paths are once-per
	// projectile / impact and may touch engine objects that are not designed
	// for concurrent plugin-side access.
	std::mutex g_projectileFirstMutex;
	std::mutex g_projectileImpactMutex;

	std::unordered_set<std::uint64_t>
		g_observedProjectileKeys;

	std::deque<std::shared_ptr<FireShotState>>
		g_recentFireShots;

	std::unordered_map<
		std::uint64_t,
		ProjectileBinding>
		g_projectileBindings;


	// ============================================================
	// Utility
	// ============================================================

	const char* SafeName(
		RE::TESForm* a_form)
	{
		if (!a_form) {
			return "<None>";
		}


		const char* name =
			a_form->GetName();


		if (!name ||
			name[0] == '\0') {

			return "<Unnamed>";
		}


		return name;
	}


	bool EntryMatchesWeapon(
		RE::InventoryEntryData* a_entry,
		RE::TESObjectWEAP* a_weapon)
	{
		if (!a_entry ||
			!a_weapon) {

			return false;
		}


		return
			a_entry->object ==
			a_weapon;
	}


	bool IsTwoHandedMelee(
		RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return false;
		}


		return
			a_weapon->IsTwoHandedSword() ||
			a_weapon->IsTwoHandedAxe();
	}


	WeaponDamageClass ClassifyWeapon(
		RE::TESObjectWEAP* a_weapon)
	{
		if (!a_weapon) {
			return
				WeaponDamageClass::kUnsupported;
		}


		if (a_weapon->IsOneHandedSword() ||
			a_weapon->IsOneHandedDagger() ||
			a_weapon->IsOneHandedAxe() ||
			a_weapon->IsOneHandedMace()) {

			return
				WeaponDamageClass::kOneHanded;
		}


		if (a_weapon->IsTwoHandedSword() ||
			a_weapon->IsTwoHandedAxe()) {

			return
				WeaponDamageClass::kTwoHanded;
		}


		if (a_weapon->IsBow() ||
			a_weapon->IsCrossbow()) {

			return
				WeaponDamageClass::kRanged;
		}


		return
			WeaponDamageClass::kUnsupported;
	}


	float GetWeaponDamageMultiplier(
		WeaponDamageClass a_class)
	{
		switch (a_class)
		{
		case WeaponDamageClass::kOneHanded:
			return
				kOneHandDamageMult;

		case WeaponDamageClass::kTwoHanded:
			return
				kTwoHandDamageMult;

		case WeaponDamageClass::kRanged:
			return
				kRangedDamageMult;

		default:
			return
				0.0F;
		}
	}


	// ============================================================
	// Actor Values
	// ============================================================

	ActorValueSnapshot GetActorValueSnapshot(
		RE::Actor* a_actor,
		RE::ActorValue a_actorValue)
	{
		ActorValueSnapshot result{};


		if (!a_actor) {
			return result;
		}


		auto* owner =
			a_actor->
				AsActorValueOwner();


		if (!owner) {
			return result;
		}


		const float current =
			owner->
				GetActorValue(
					a_actorValue);


		const float permanent =
			owner->
				GetPermanentActorValue(
					a_actorValue);


		const float temporary =
			a_actor->
				GetActorValueModifier(
					RE::ACTOR_VALUE_MODIFIER::
						kTemporary,
					a_actorValue);


		const float maximum =
			permanent +
			temporary;


		if (!std::isfinite(current) ||
			!std::isfinite(maximum)) {

			return result;
		}


		result.current =
			current;


		result.maximum =
			(maximum > 0.0F) ?
				maximum :
				0.0F;


		result.valid =
			true;


		return result;
	}


	float GetConjuration(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return 0.0F;
		}

		auto* owner = a_actor->AsActorValueOwner();
		if (!owner) {
			return 0.0F;
		}

		const float value = owner->GetActorValue(RE::ActorValue::kConjuration);
		return std::isfinite(value) ? value : 0.0F;
	}


	// ============================================================
	// Ammo
	// ============================================================

	RangedAmmoSnapshot GetAmmoSnapshot(
		RE::TESAmmo* a_ammo)
	{
		RangedAmmoSnapshot result{};

		if (!a_ammo) {
			return result;
		}

		const auto& runtime = a_ammo->GetRuntimeData();
		result.ammo = a_ammo;
		result.projectileBase = runtime.data.projectile;
		return result;
	}


	// ============================================================
	// Hand / Inventory Instance - Melee
	// ============================================================

	HitWeaponContext ResolveHitWeaponContext(
		RE::PlayerCharacter* a_player,
		RE::TESObjectWEAP* a_weapon,
		RE::HitData& a_data)
	{
		HitWeaponContext result{};


		if (!a_player ||
			!a_weapon) {

			return result;
		}


		if (IsTwoHandedMelee(
				a_weapon)) {

			auto* rightEntry =
				a_player->
					GetEquippedEntryData(
						false);


			if (EntryMatchesWeapon(
					rightEntry,
					a_weapon)) {

				result.entry =
					rightEntry;

				result.hand =
					HitHand::kBoth;

				return result;
			}


			auto* leftEntry =
				a_player->
					GetEquippedEntryData(
						true);


			if (EntryMatchesWeapon(
					leftEntry,
					a_weapon)) {

				result.entry =
					leftEntry;

				result.hand =
					HitHand::kBoth;

				return result;
			}


			return result;
		}


		if (a_data.attackData) {


			result.attackDataLeft =
				a_data.attackData->
					IsLeftAttack();
		}


		result.offhandFlag =
			a_data.flags.any(
				static_cast<
					RE::HitData::Flag>(
						kHitDataOffhandMask));


		const bool resolvedLeft =
			result.offhandFlag ||
			result.attackDataLeft;


		auto* preferredEntry =
			a_player->
				GetEquippedEntryData(
					resolvedLeft);


		if (EntryMatchesWeapon(
				preferredEntry,
				a_weapon)) {

			result.entry =
				preferredEntry;


			result.hand =
				resolvedLeft ?
					HitHand::kLeft :
					HitHand::kRight;


			return result;
		}


		auto* rightEntry =
			a_player->
				GetEquippedEntryData(
					false);


		auto* leftEntry =
			a_player->
				GetEquippedEntryData(
					true);


		const bool rightMatches =
			EntryMatchesWeapon(
				rightEntry,
				a_weapon);


		const bool leftMatches =
			EntryMatchesWeapon(
				leftEntry,
				a_weapon);


		if (rightMatches &&
			!leftMatches) {

			result.entry =
				rightEntry;

			result.hand =
				HitHand::kRight;

			return result;
		}


		if (leftMatches &&
			!rightMatches) {

			result.entry =
				leftEntry;

			result.hand =
				HitHand::kLeft;

			return result;
		}


		return result;
	}


	// ============================================================
	// Inventory Instance - Ranged
	// ============================================================

	HitWeaponContext ResolveRangedWeaponContext(
		RE::PlayerCharacter* a_player,
		RE::TESObjectWEAP* a_weapon)
	{
		HitWeaponContext result{};


		if (!a_player ||
			!a_weapon) {

			return result;
		}


		auto* rightEntry =
			a_player->
				GetEquippedEntryData(
					false);


		if (EntryMatchesWeapon(
				rightEntry,
				a_weapon)) {

			result.entry =
				rightEntry;

			result.hand =
				HitHand::kBoth;

			return result;
		}


		auto* leftEntry =
			a_player->
				GetEquippedEntryData(
					true);


		if (EntryMatchesWeapon(
				leftEntry,
				a_weapon)) {

			result.entry =
				leftEntry;

			result.hand =
				HitHand::kBoth;

			return result;
		}


		return result;
	}


	// ============================================================
	// Enchantment
	// ============================================================

	RE::EnchantmentItem* ResolveEnchantment(
		RE::InventoryEntryData* a_entry,
		RE::TESObjectWEAP* a_weapon)
	{
		if (a_entry) {

			if (auto* enchantment =
					a_entry->
						GetEnchantment()) {

				return enchantment;
			}
		}


		if (a_weapon) {
			return
				a_weapon->
					formEnchanting;
		}


		return nullptr;
	}


	std::optional<ConversionInfo>
	AnalyzeEnchantment(
		RE::EnchantmentItem* a_enchantment)
	{
		if (!a_enchantment ||
			!g_conversionKeyword) {

			return std::nullopt;
		}


		RE::Effect* matchedEffect =
			nullptr;


		for (auto* effect :
			a_enchantment->effects) {

			if (!effect ||
				!effect->baseEffect) {

				continue;
			}


			if (!effect->
					baseEffect->
					HasKeyword(
						g_conversionKeyword)) {

				continue;
			}


			if (matchedEffect) {

				logger::warn(
					"[ENCH-INVALID] "
					"Enchantment={:08X} \"{}\" | "
					"Reason=MultipleConversionEffects",

					a_enchantment->
						GetFormID(),

					SafeName(
						a_enchantment));


				return std::nullopt;
			}


			matchedEffect =
				effect;
		}


		if (!matchedEffect ||
			!matchedEffect->baseEffect) {

			return std::nullopt;
		}


		const float magnitudePercent =
			matchedEffect->
				effectItem.magnitude;


		if (!std::isfinite(
				magnitudePercent) ||
			magnitudePercent <= 0.0F) {

			return std::nullopt;
		}


		ConversionInfo info{};


		info.baseConversionRate =
			magnitudePercent *
			0.01F;


		info.conversionEffect =
			matchedEffect->
				baseEffect;


		info.scaleWithConjuration =
			g_boundConversionEffect &&
			info.conversionEffect ==
				g_boundConversionEffect;


		return info;
	}


	const ConversionInfo* GetConversionInfo(
		RE::EnchantmentItem* a_enchantment)
	{
		if (!a_enchantment) {
			return nullptr;
		}


		std::lock_guard lock{
			g_enchantmentCacheMutex
		};


		const auto found =
			g_enchantmentCache.find(
				a_enchantment);


		if (found !=
			g_enchantmentCache.end()) {

			if (!found->second) {
				return nullptr;
			}


			return
				std::addressof(
					found->second.value());
		}


		auto analyzed =
			AnalyzeEnchantment(
				a_enchantment);


		auto insertion =
			g_enchantmentCache.emplace(
				a_enchantment,
				analyzed);

		auto it = insertion.first;

		if (!it->second) {
			return nullptr;
		}


		return
			std::addressof(
				it->second.value());
	}


	void RebuildEnchantmentCache()
	{
		auto* dataHandler =
			RE::TESDataHandler::GetSingleton();


		if (!dataHandler ||
			!g_conversionKeyword) {

			std::lock_guard lock{
				g_enchantmentCacheMutex
			};

			g_enchantmentCache.clear();
			return;
		}


		auto& enchantments =
			dataHandler->GetFormArray<RE::EnchantmentItem>();


		std::unordered_map<
			RE::EnchantmentItem*,
			std::optional<ConversionInfo>> rebuilt;

		rebuilt.reserve(
			enchantments.size() + 32);


		for (auto* enchantment : enchantments) {

			if (!enchantment) {
				continue;
			}


			auto analyzed =
				AnalyzeEnchantment(
					enchantment);


			rebuilt.emplace(
				enchantment,
				std::move(analyzed));
		}


		{
			std::lock_guard lock{
				g_enchantmentCacheMutex
			};

			g_enchantmentCache.swap(
				rebuilt);

		}

	}


	// ============================================================
	// Bound Scaling
	// ============================================================

	float GetBoundConversionRate(
		float a_conjuration)
	{
		if (a_conjuration >= 100.0F) {
			return 0.10F;
		}


		if (a_conjuration >= 75.0F) {
			return 0.0875F;
		}


		if (a_conjuration >= 50.0F) {
			return 0.075F;
		}


		if (a_conjuration >= 25.0F) {
			return 0.0625F;
		}


		return 0.05F;
	}


	float GetEffectiveRangedRate(
		const ConversionInfo* a_conversionInfo,
		float a_conjuration)
	{
		if (!a_conversionInfo) {
			return 0.0F;
		}


		if (a_conversionInfo->
			scaleWithConjuration) {

			return
				GetBoundConversionRate(
					a_conjuration);
		}


		return
			a_conversionInfo->
				baseConversionRate;
	}


	// ============================================================
	// Native Poison Guard
	// ============================================================

	bool ExtraListMatchesEquippedHand(
		RE::ExtraDataList* a_list,
		HitHand a_hand)
	{
		if (!a_list) {
			return false;
		}


		switch (a_hand)
		{
		case HitHand::kRight:

			return
				a_list->
					HasType(
						RE::ExtraDataType::
							kWorn);


		case HitHand::kLeft:

			return
				a_list->
					HasType(
						RE::ExtraDataType::
							kWornLeft);


		case HitHand::kBoth:

			return
				a_list->
					HasType(
						RE::ExtraDataType::
							kWorn) ||
				a_list->
					HasType(
						RE::ExtraDataType::
							kWornLeft);


		default:

			return false;
		}
	}


	bool ClearPoisonFromEquippedEntry(
		RE::InventoryEntryData* a_entry,
		HitHand a_hand)
	{
		if (!a_entry ||
			!a_entry->extraLists) {

			return false;
		}


		if (!a_entry->IsPoisoned()) {
			return false;
		}


		bool removed =
			false;


		for (auto* extraList :
			*a_entry->extraLists) {

			if (!extraList) {
				continue;
			}


			if (!ExtraListMatchesEquippedHand(
					extraList,
					a_hand)) {

				continue;
			}


			if (!extraList->
					HasType(
						RE::ExtraDataType::
							kPoison)) {

				continue;
			}


			if (extraList->
					RemoveByType(
						RE::ExtraDataType::
							kPoison)) {

				removed =
					true;
			}
		}


		return removed;
	}


	class PoisonActionSink final :
		public RE::BSTEventSink<
			SKSE::ActionEvent>
	{
	public:

		static PoisonActionSink*
			GetSingleton()
		{
			static PoisonActionSink singleton;

			return
				std::addressof(
					singleton);
		}


		RE::BSEventNotifyControl ProcessEvent(
			const SKSE::ActionEvent* a_event,
			RE::BSTEventSource<
				SKSE::ActionEvent>*) override
		{
			if (!a_event) {

				return
					RE::BSEventNotifyControl::
						kContinue;
			}


			if (a_event->type !=
				SKSE::ActionEvent::Type::
					kWeaponSwing) {

				return
					RE::BSEventNotifyControl::
						kContinue;
			}


			auto* player =
				RE::PlayerCharacter::
					GetSingleton();


			if (!player ||
				a_event->actor != player) {

				return
					RE::BSEventNotifyControl::
						kContinue;
			}


			HitHand hand =
				HitHand::kUnknown;


			bool left =
				false;


			if (a_event->slot ==
				SKSE::ActionEvent::Slot::
					kLeft) {

				hand =
					HitHand::kLeft;

				left =
					true;

			} else if (
				a_event->slot ==
				SKSE::ActionEvent::Slot::
					kRight) {

				hand =
					HitHand::kRight;

				left =
					false;

			} else {

				return
					RE::BSEventNotifyControl::
						kContinue;
			}


			auto* entry =
				player->
					GetEquippedEntryData(
						left);


			if (!entry) {

				return
					RE::BSEventNotifyControl::
						kContinue;
			}


			auto* weapon =
				a_event->sourceForm ?
					a_event->
						sourceForm->
						As<
							RE::TESObjectWEAP>() :
					nullptr;


			if (!weapon ||
				!EntryMatchesWeapon(
					entry,
					weapon)) {

				return
					RE::BSEventNotifyControl::
						kContinue;
			}


			auto* enchantment =
				ResolveEnchantment(
					entry,
					weapon);


			const auto* conversionInfo =
				GetConversionInfo(
					enchantment);


			if (!conversionInfo) {

				return
					RE::BSEventNotifyControl::
						kContinue;
			}


			ClearPoisonFromEquippedEntry(
				entry,
				hand);


			return
				RE::BSEventNotifyControl::
					kContinue;
		}


	private:

		PoisonActionSink() =
			default;
	};


	void RegisterPoisonGuard()
	{
		if (g_poisonSinkRegistered) {
			return;
		}


		auto* source =
			SKSE::
				GetActionEventSource();


		if (!source) {

			logger::error(
				"Failed to get "
				"SKSE ActionEvent source");

			return;
		}


		source->
			AddEventSink(
				PoisonActionSink::
					GetSingleton());


		g_poisonSinkRegistered =
			true;

	}


	std::shared_ptr<FireShotState> CreateFireShotState(
		RangedFireSource a_fireSource,
		RE::PlayerCharacter* a_player,
		RE::TESObjectWEAP* a_weapon,
		const RangedAmmoSnapshot& a_eventAmmo,
		float a_effectiveRate,
		const ActorValueSnapshot& a_magicka);


	// ============================================================
	// Ranged primary fire source - TESPlayerBowShotEvent
	// ============================================================

	class PlayerBowShotSink final :
		public RE::BSTEventSink<RE::TESPlayerBowShotEvent>
	{
	public:
		static PlayerBowShotSink* GetSingleton()
		{
			static PlayerBowShotSink singleton;
			return std::addressof(singleton);
		}

		RE::BSEventNotifyControl ProcessEvent(
			const RE::TESPlayerBowShotEvent* a_event,
			RE::BSTEventSource<RE::TESPlayerBowShotEvent>*) override
		{
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return RE::BSEventNotifyControl::kContinue;
			}

			auto* weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(a_event->weapon);
			auto* ammoForm = RE::TESForm::LookupByID<RE::TESAmmo>(a_event->ammo);

			if (!weapon || ClassifyWeapon(weapon) != WeaponDamageClass::kRanged) {
				return RE::BSEventNotifyControl::kContinue;
			}

			const auto context = ResolveRangedWeaponContext(player, weapon);
			auto* enchantment = ResolveEnchantment(context.entry, weapon);
			const auto* conversionInfo = GetConversionInfo(enchantment);

			if (!conversionInfo) {
				return RE::BSEventNotifyControl::kContinue;
			}

			const float conjuration = GetConjuration(player);
			const float effectiveRate = GetEffectiveRangedRate(conversionInfo, conjuration);
			const auto magicka = GetActorValueSnapshot(player, RE::ActorValue::kMagicka);
			const auto eventAmmo = GetAmmoSnapshot(ammoForm);

			std::lock_guard fireCreationLock{ g_fireCreationMutex };

			CreateFireShotState(
				RangedFireSource::kBowShotEvent,
				player,
				weapon,
				eventAmmo,
				effectiveRate,
				magicka);

			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		PlayerBowShotSink() = default;
	};


	void RegisterRangedFireSource()
	{
		if (g_playerBowShotSinkRegistered) {
			return;
		}

		auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
		if (!holder) {
			logger::error("ScriptEventSourceHolder is null");
			return;
		}

		holder->AddEventSink<RE::TESPlayerBowShotEvent>(
			PlayerBowShotSink::GetSingleton());

		g_playerBowShotSinkRegistered = true;
	}


	// ============================================================
	// Ranged projectile core - v1.0.0
	// ============================================================

	std::uint64_t MakeProjectileKey(RE::ArrowProjectile* a_projectile)
	{
		if (!a_projectile) {
			return 0;
		}

		const auto formID = static_cast<std::uint64_t>(a_projectile->GetFormID());
		const auto pointerValue = static_cast<std::uint64_t>(
			reinterpret_cast<std::uintptr_t>(a_projectile));

		auto key = pointerValue ^ (formID * 0x9E3779B185EBCA87ULL);
		if (key == 0) {
			key = pointerValue ? pointerValue : formID;
		}

		return key;
	}


	void PruneRangedStateLocked(std::chrono::steady_clock::time_point a_now)
	{
		while (!g_recentFireShots.empty()) {
			const auto& front = g_recentFireShots.front();
			const bool tooOld = !front || a_now - front->createdAt > kFireBindWindow;
			const bool tooMany = g_recentFireShots.size() > kRecentFireShotLimit;

			if (!tooOld && !tooMany) {
				break;
			}

			g_recentFireShots.pop_front();
		}

		if (g_projectileBindings.size() <= kBindingPruneThreshold) {
			return;
		}

		for (auto it = g_projectileBindings.begin(); it != g_projectileBindings.end();) {
			if (a_now - it->second.firstSeenAt > kBindingPruneAge) {
				it = g_projectileBindings.erase(it);
			} else {
				++it;
			}
		}
	}


	std::shared_ptr<FireShotState> CreateFireShotState(
		RangedFireSource a_fireSource,
		RE::PlayerCharacter* a_player,
		RE::TESObjectWEAP* a_weapon,
		const RangedAmmoSnapshot& a_eventAmmo,
		float a_effectiveRate,
		const ActorValueSnapshot& a_magicka)
	{
		if (!a_player || !a_weapon) {
			return nullptr;
		}

		auto shot = std::make_shared<FireShotState>();
		shot->fireSource = a_fireSource;
		shot->weapon = a_weapon->GetFormID();
		shot->ammo = a_eventAmmo.ammo ? a_eventAmmo.ammo->GetFormID() : 0;
		shot->projectileBase = a_eventAmmo.projectileBase ?
			a_eventAmmo.projectileBase->GetFormID() : 0;

		shot->prepayCost = a_magicka.valid ?
			a_magicka.maximum * kRangedPrepayRate : 0.0F;
		shot->refundCost = a_magicka.valid ?
			a_magicka.maximum * kRangedRefundRate : 0.0F;
		shot->magicDamagePerHit = a_magicka.valid ?
			a_magicka.maximum * a_effectiveRate * kRangedDamageMult : 0.0F;
		shot->createdAt = std::chrono::steady_clock::now();

		auto* playerAV = a_player->AsActorValueOwner();
		if (playerAV &&
			a_magicka.valid &&
			a_magicka.maximum > 0.0F &&
			std::isfinite(shot->prepayCost)) {

			const float currentMagicka = playerAV->GetActorValue(RE::ActorValue::kMagicka);
			if (currentMagicka >= shot->prepayCost) {
				playerAV->DamageActorValue(RE::ActorValue::kMagicka, shot->prepayCost);
				shot->paid = true;
				shot->approved = true;
			}
		}

		{
			std::lock_guard lock{ g_rangedStateMutex };
			PruneRangedStateLocked(shot->createdAt);
			g_recentFireShots.push_back(shot);
		}

		return shot;
	}


	std::shared_ptr<FireShotState> BindProjectileToFire(
		RE::ArrowProjectile* a_projectile,
		RE::PlayerCharacter* a_player,
		RE::TESObjectWEAP* a_weapon,
		const RangedAmmoSnapshot& a_ammoSnapshot,
		RE::BGSProjectile* a_projectileBase,
		float a_effectiveRate,
		const ActorValueSnapshot& a_magicka)
	{
		if (!a_projectile || !a_player || !a_weapon) {
			return nullptr;
		}

		const auto key = MakeProjectileKey(a_projectile);
		if (key == 0) {
			return nullptr;
		}

		const auto projectilePtr = reinterpret_cast<std::uintptr_t>(a_projectile);
		const auto now = std::chrono::steady_clock::now();
		const auto weaponID = a_weapon->GetFormID();
		const auto ammoID = a_ammoSnapshot.ammo ? a_ammoSnapshot.ammo->GetFormID() : 0;
		const auto projectileBaseID = a_projectileBase ? a_projectileBase->GetFormID() : 0;

		std::shared_ptr<FireShotState> shot;

		std::lock_guard fireCreationLock{ g_fireCreationMutex };

		{
			std::lock_guard stateLock{ g_rangedStateMutex };
			PruneRangedStateLocked(now);

			if (const auto found = g_projectileBindings.find(key);
				found != g_projectileBindings.end() &&
				found->second.projectilePtr == projectilePtr) {
				return found->second.shot;
			}

			for (auto it = g_recentFireShots.rbegin(); it != g_recentFireShots.rend(); ++it) {
				const auto& candidate = *it;
				if (!candidate || candidate->weapon != weaponID) {
					continue;
				}

				if (candidate->ammo != 0 && ammoID != 0 && candidate->ammo != ammoID) {
					continue;
				}

				if (candidate->projectileBase != 0 &&
					projectileBaseID != 0 &&
					candidate->projectileBase != projectileBaseID) {
					continue;
				}

				bool windowMatches = false;
				{
					std::lock_guard shotLock{ candidate->mutex };

					if (candidate->projectileCount == 0) {
						const auto window = candidate->fireSource == RangedFireSource::kBowShotEvent ?
							kFireBindWindow : kMultiProjectileJoinWindow;
						windowMatches = now - candidate->createdAt <= window;
					} else if (candidate->firstProjectileAt !=
						std::chrono::steady_clock::time_point{}) {
						windowMatches = now - candidate->firstProjectileAt <=
							kMultiProjectileJoinWindow;
					}
				}

				if (windowMatches) {
					shot = candidate;
					break;
				}
			}
		}

		if (!shot) {
			shot = CreateFireShotState(
				RangedFireSource::kProjectileFallback,
				a_player,
				a_weapon,
				a_ammoSnapshot,
				a_effectiveRate,
				a_magicka);

			if (!shot) {
				return nullptr;
			}
		}

		{
			std::lock_guard stateLock{ g_rangedStateMutex };

			if (const auto found = g_projectileBindings.find(key);
				found != g_projectileBindings.end() &&
				found->second.projectilePtr == projectilePtr) {
				return found->second.shot;
			}

			ProjectileBinding binding{};
			binding.projectilePtr = projectilePtr;
			binding.firstSeenAt = now;
			binding.shot = shot;
			g_projectileBindings.insert_or_assign(key, std::move(binding));
		}

		{
			std::lock_guard shotLock{ shot->mutex };
			if (shot->projectileCount == 0) {
				shot->firstProjectileAt = now;
			}
			++shot->projectileCount;
		}

		return shot;
	}


	std::shared_ptr<FireShotState> GetBoundFireShot(RE::ArrowProjectile* a_projectile)
	{
		if (!a_projectile) {
			return nullptr;
		}

		const auto key = MakeProjectileKey(a_projectile);
		if (key == 0) {
			return nullptr;
		}

		const auto projectilePtr = reinterpret_cast<std::uintptr_t>(a_projectile);
		std::lock_guard lock{ g_rangedStateMutex };

		const auto found = g_projectileBindings.find(key);
		if (found == g_projectileBindings.end() ||
			found->second.projectilePtr != projectilePtr) {
			return nullptr;
		}

		return found->second.shot;
	}


	void ObserveArrowProjectileFirst(RE::ArrowProjectile* a_projectile)
	{
		if (!a_projectile) {
			return;
		}

		std::lock_guard firstPathLock{ g_projectileFirstMutex };

		const auto& runtime = a_projectile->GetProjectileRuntimeData();
		auto shooterPtr = runtime.shooter.get();
		auto* shooter = shooterPtr.get();
		auto* player = RE::PlayerCharacter::GetSingleton();

		if (!player || shooter != player) {
			return;
		}

		auto* weapon = runtime.weaponSource;
		if (!weapon || ClassifyWeapon(weapon) != WeaponDamageClass::kRanged) {
			return;
		}

		const auto key = MakeProjectileKey(a_projectile);
		if (key == 0) {
			return;
		}

		{
			std::lock_guard lock{ g_rangedStateMutex };
			if (!g_observedProjectileKeys.insert(key).second) {
				return;
			}
		}

		auto* ammo = runtime.ammoSource;
		const auto ammoInfo = GetAmmoSnapshot(ammo);
		auto* projectileBase = a_projectile->GetProjectileBase();

		const auto context = ResolveRangedWeaponContext(player, weapon);
		auto* equippedEnchantment = ResolveEnchantment(context.entry, weapon);

		auto& arrowRuntime = a_projectile->GetArrowRuntimeData();
		auto* authoritativeEnchantment = arrowRuntime.enchantItem ?
			arrowRuntime.enchantItem : equippedEnchantment;

		const auto* conversionInfo = GetConversionInfo(authoritativeEnchantment);
		if (!conversionInfo) {
			return;
		}

		if (arrowRuntime.poison) {
			arrowRuntime.poison = nullptr;
		}

		if (context.entry) {
			ClearPoisonFromEquippedEntry(context.entry, HitHand::kRight);
		}

		const float conjuration = GetConjuration(player);
		const float effectiveRate = GetEffectiveRangedRate(conversionInfo, conjuration);
		const auto magicka = GetActorValueSnapshot(player, RE::ActorValue::kMagicka);

		BindProjectileToFire(
			a_projectile,
			player,
			weapon,
			ammoInfo,
			projectileBase,
			effectiveRate,
			magicka);
	}


	bool TryRefundFireShot(
		const std::shared_ptr<FireShotState>& a_shot,
		RE::PlayerCharacter* a_player)
	{
		if (!a_shot || !a_player) {
			return false;
		}

		auto* playerAV = a_player->AsActorValueOwner();
		if (!playerAV) {
			return false;
		}

		std::lock_guard lock{ a_shot->mutex };

		if (!a_shot->approved ||
			!a_shot->paid ||
			a_shot->refunded ||
			!(a_shot->refundCost > 0.0F) ||
			!std::isfinite(a_shot->refundCost)) {
			return false;
		}

		playerAV->RestoreActorValue(RE::ActorValue::kMagicka, a_shot->refundCost);
		a_shot->refunded = true;
		return true;
	}


	bool TryApplyMagicDamage(
		const std::shared_ptr<FireShotState>& a_shot,
		RE::TESObjectREFR* a_target)
	{
		if (!a_shot || !a_target) {
			return false;
		}

		auto* targetActor = a_target->As<RE::Actor>();
		if (!targetActor) {
			return false;
		}

		auto* targetAV = targetActor->AsActorValueOwner();
		if (!targetAV) {
			return false;
		}

		std::lock_guard lock{ a_shot->mutex };

		if (!a_shot->approved ||
			!a_shot->paid ||
			!(a_shot->magicDamagePerHit > 0.0F) ||
			!std::isfinite(a_shot->magicDamagePerHit)) {
			return false;
		}

		targetAV->DamageActorValue(RE::ActorValue::kHealth, a_shot->magicDamagePerHit);
		return true;
	}


	void ObserveArrowProjectileImpact(
		RE::ArrowProjectile* a_projectile,
		RE::TESObjectREFR* a_target)
	{
		if (!a_projectile || !a_target || !a_target->As<RE::Actor>()) {
			return;
		}

		std::lock_guard impactPathLock{ g_projectileImpactMutex };

		const auto& runtime = a_projectile->GetProjectileRuntimeData();
		auto shooterPtr = runtime.shooter.get();
		auto* shooter = shooterPtr.get();
		auto* player = RE::PlayerCharacter::GetSingleton();

		if (!player || shooter != player) {
			return;
		}

		auto* weapon = runtime.weaponSource;
		if (!weapon || ClassifyWeapon(weapon) != WeaponDamageClass::kRanged) {
			return;
		}

		auto shot = GetBoundFireShot(a_projectile);
		if (!shot) {
			return;
		}

		TryRefundFireShot(shot, player);
		TryApplyMagicDamage(shot, a_target);
	}

	namespace ArrowProjectileHook
	{
		namespace UpdateImplHook
		{
			using func_t =
				void (*)(
					RE::ArrowProjectile*,
					float);


			static inline func_t func{
				nullptr
			};


			void thunk(
				RE::ArrowProjectile* a_projectile,
				float a_delta)
			{
				ObserveArrowProjectileFirst(a_projectile);


				func(
					a_projectile,
					a_delta);
			}
		}


		namespace AddImpactHook
		{
			using func_t =
				void (*)(
					RE::ArrowProjectile*,
					RE::TESObjectREFR*,
					const RE::NiPoint3&,
					const RE::NiPoint3&,
					RE::hkpCollidable*,
					std::int32_t,
					std::uint32_t);


			static inline func_t func{
				nullptr
			};


			void thunk(
				RE::ArrowProjectile* a_projectile,
				RE::TESObjectREFR* a_ref,
				const RE::NiPoint3& a_targetLoc,
				const RE::NiPoint3& a_velocity,
				RE::hkpCollidable* a_collidable,
				std::int32_t a_arg6,
				std::uint32_t a_arg7)
			{
				// Point-blank impact can arrive before the first Update.
				ObserveArrowProjectileFirst(a_projectile);


				ObserveArrowProjectileImpact(
					a_projectile,
					a_ref);


				func(
					a_projectile,
					a_ref,
					a_targetLoc,
					a_velocity,
					a_collidable,
					a_arg6,
					a_arg7);
			}
		}


		void Install()
		{
			if (g_arrowProjectileHookInstalled) {
				return;
			}


			REL::Relocation<std::uintptr_t> vtable{
				RE::ArrowProjectile::VTABLE[0]
			};


			UpdateImplHook::func =
				reinterpret_cast<
					UpdateImplHook::func_t>(
						vtable.write_vfunc(
							0xAB,
							UpdateImplHook::thunk));


			AddImpactHook::func =
				reinterpret_cast<
					AddImpactHook::func_t>(
						vtable.write_vfunc(
							0xBD,
							AddImpactHook::thunk));


			g_arrowProjectileHookInstalled =
				true;

		}
	}


	// ============================================================
	// Forms
	// ============================================================

	bool ResolveForms()
	{
		auto* dataHandler =
			RE::TESDataHandler::
				GetSingleton();


		if (!dataHandler) {

			logger::error(
				"TESDataHandler is null");

			return false;
		}


		g_conversionKeyword =
			dataHandler->
				LookupForm<
					RE::BGSKeyword>(
						kConversionKeywordLocalID,
						kPluginFileName);


		g_boundConversionEffect =
			dataHandler->
				LookupForm<
					RE::EffectSetting>(
						kBoundConversionEffectLocalID,
						kPluginFileName);


		if (!g_conversionKeyword) {

			logger::error(
				"Failed to resolve "
				"conversion keyword");

			return false;
		}


		if (!g_boundConversionEffect) {

			logger::error(
				"Failed to resolve "
				"Bound conversion MGEF");

			return false;
		}


		return true;
	}


	// ============================================================
	// Physical Damage Block - Melee only
	// ============================================================

	void BlockPhysicalDamage(RE::HitData& a_data)
	{
		a_data.totalDamage = 0.0F;
		a_data.physicalDamage = 0.0F;
		a_data.targetedLimbDamage = 0.0F;
		a_data.resistedPhysicalDamage = 0.0F;
	}


	// ============================================================
	// Melee transaction
	// ============================================================

	TransactionCalculation CalculateTransaction(
		RE::PlayerCharacter* a_player,
		const ConversionInfo& a_info,
		WeaponDamageClass a_weaponClass,
		bool a_powerAttack)
	{
		TransactionCalculation result{};


		if (!a_player) {
			return result;
		}


		const auto magicka =
			GetActorValueSnapshot(
				a_player,
				RE::ActorValue::
					kMagicka);


		if (!magicka.valid) {
			return result;
		}


		const float weaponMult =
			GetWeaponDamageMultiplier(
				a_weaponClass);


		if (!(weaponMult > 0.0F)) {
			return result;
		}


		float effectiveRate =
			a_info.baseConversionRate;


		float conjuration =
			0.0F;


		if (a_info.scaleWithConjuration) {

			auto* owner =
				a_player->
					AsActorValueOwner();


			if (!owner) {
				return result;
			}


			conjuration =
				owner->
					GetActorValue(
						RE::ActorValue::
							kConjuration);


			effectiveRate =
				GetBoundConversionRate(
					conjuration);
		}


		const float costRate =
			a_powerAttack ?
				kPowerCostRate :
				kNormalCostRate;


		const float powerMult =
			a_powerAttack ?
				kPowerDamageMult :
				kNormalDamageMult;


		const float cost =
			magicka.maximum *
			costRate;


		const float damage =
			magicka.maximum *
			effectiveRate *
			weaponMult *
			powerMult;


		if (!std::isfinite(cost) ||
			!std::isfinite(damage)) {

			return result;
		}


		result.cost =
			(cost > 0.0F) ? cost : 0.0F;

		result.damage =
			(damage > 0.0F) ? damage : 0.0F;

		result.valid = true;


		return result;
	}


	// ============================================================
	// HitData processor
	// ============================================================

	void ProcessHitData(
		RE::TESObjectREFR* a_target,
		RE::Actor* a_aggressor,
		RE::TESObjectWEAP* a_weapon,
		RE::HitData& a_data,
		bool a_allowPhysicalMutation)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();

		if (!player ||
			a_aggressor != player ||
			!a_target ||
			!a_weapon) {
			return;
		}

		auto* targetActor = a_target->As<RE::Actor>();
		if (!targetActor) {
			return;
		}

		const auto weaponClass = ClassifyWeapon(a_weapon);
		const bool isMelee =
			weaponClass == WeaponDamageClass::kOneHanded ||
			weaponClass == WeaponDamageClass::kTwoHanded;

		// Ranged gameplay is handled exclusively by the ArrowProjectile path.
		if (!isMelee) {
			return;
		}

		const bool powerAttack = a_data.flags.any(RE::HitData::Flag::kPowerAttack);
		const auto hitContext = ResolveHitWeaponContext(player, a_weapon, a_data);
		auto* enchantment = ResolveEnchantment(hitContext.entry, a_weapon);
		const auto* conversionInfo = GetConversionInfo(enchantment);

		if (!conversionInfo) {
			return;
		}

		const bool usingBaseEnchantment = a_weapon->formEnchanting == enchantment;
		if (!hitContext.entry && !usingBaseEnchantment) {
			return;
		}

		if (hitContext.entry) {
			ClearPoisonFromEquippedEntry(hitContext.entry, hitContext.hand);
		}

		if (kEnablePhysicalMutation && a_allowPhysicalMutation) {
			BlockPhysicalDamage(a_data);
		}

		const auto tx = CalculateTransaction(
			player,
			*conversionInfo,
			weaponClass,
			powerAttack);

		auto* targetAV = targetActor->AsActorValueOwner();
		if (!tx.valid || !targetAV) {
			return;
		}

		auto* playerAV = player->AsActorValueOwner();
		if (!playerAV) {
			return;
		}

		const float currentMagicka = playerAV->GetActorValue(RE::ActorValue::kMagicka);
		if (currentMagicka < tx.cost) {
			return;
		}

		playerAV->DamageActorValue(RE::ActorValue::kMagicka, tx.cost);
		targetAV->DamageActorValue(RE::ActorValue::kHealth, tx.damage);
	}


	// ============================================================
	// Existing HitData Hook
	// ============================================================

	namespace WeaponHitHook
	{
		static inline constexpr
			REL::RelocationID
			kTargetID{
				37633,
				38586
			};


		static inline constexpr
			REL::VariantOffset
			kTargetOffset{
				0x16A,
				0xFA,
				0x0
			};


		namespace SE
		{
			using func_t =
				void (*)(
					RE::ScriptEventSourceHolder*,
					RE::NiPointer<
						RE::TESObjectREFR>&,
					RE::NiPointer<
						RE::TESObjectREFR>&,
					RE::FormID,
					RE::FormID,
					RE::HitData&);


			static inline func_t func{
				nullptr
			};


			void thunk(
				RE::ScriptEventSourceHolder*
					a_holder,

				RE::NiPointer<
					RE::TESObjectREFR>&
					a_target,

				RE::NiPointer<
					RE::TESObjectREFR>&
					a_aggressor,

				RE::FormID a_source,

				RE::FormID a_projectile,

				RE::HitData& a_data)
			{
				auto* target =
					a_target.get();


				auto* aggressorRef =
					a_aggressor.get();


				auto* aggressor =
					aggressorRef ?
						aggressorRef->
							As<RE::Actor>() :
						nullptr;


				ProcessHitData(
					target,
					aggressor,
					a_data.weapon,
					a_data,
					true);


				func(
					a_holder,
					a_target,
					a_aggressor,
					a_source,
					a_projectile,
					a_data);
			}
		}


		namespace AE
		{
			using func_t =
				void (*)(
					RE::AIProcess*,
					RE::HitData&);


			static inline func_t func{
				nullptr
			};


			void thunk(
				RE::AIProcess*
					a_targetProcess,

				RE::HitData&
					a_data)
			{
				func(
					a_targetProcess,
					a_data);


				auto* target =
					a_targetProcess ?
						a_targetProcess->
							GetUserData() :
						nullptr;


				auto aggressorPtr =
					a_data.aggressor.get();


				auto* aggressor =
					aggressorPtr.get();


				ProcessHitData(
					target,
					aggressor,
					a_data.weapon,
					a_data,
					false);
			}
		}


		void Install()
		{
			if (g_hitHookInstalled) {
				return;
			}


			REL::Relocation<
				std::uintptr_t>
				target{
					kTargetID,
					kTargetOffset
				};


			auto& trampoline =
				SKSE::
					GetTrampoline();


			if (REL::Module::IsAE()) {

				const auto original =
					trampoline.
						write_call<5>(
							target.address(),
							AE::thunk);


				AE::func =
					reinterpret_cast<
						AE::func_t>(
							original);


				logger::warn(
					"AE runtime detected: melee physical mutation is disabled (unverified path)");

			} else {

				const auto original =
					trampoline.
						write_call<5>(
							target.address(),
							SE::thunk);


				SE::func =
					reinterpret_cast<
						SE::func_t>(
							original);

			}


			g_hitHookInstalled =
				true;
		}
	}
}


// ============================================================
// Logging
// ============================================================

void SetupLog()
{
	auto logsFolder =
		logger::log_directory();


	if (!logsFolder) {

		util::report_and_fail(
			"SKSE log_directory not provided, "
			"logs can't be written");
	}


	const auto* plugin =
		SKSE::PluginDeclaration::
			GetSingleton();


	const auto logName =
		plugin ?
			std::string{
				plugin->GetName()
			} + ".log" :
			"Plugin.log";


	auto logPath =
		*logsFolder /
		logName;


	auto fileSink =
		std::make_shared<
			spdlog::sinks::
				basic_file_sink_mt>(
					logPath.string(),
					true);


	std::vector<
		spdlog::sink_ptr>
		sinks{
			fileSink
		};


	auto spdlogger =
		std::make_shared<
			spdlog::logger>(
				"global",
				sinks.begin(),
				sinks.end());


	spdlog::set_default_logger(
		std::move(
			spdlogger));


	spdlog::set_pattern(
		"[%H:%M:%S.%e] "
		"[%l] "
		"[%s:%#] %v");


	spdlog::set_level(
		spdlog::level::info);


	spdlog::flush_on(
		spdlog::level::info);
}


// ============================================================
// Reset ranged runtime state
// ============================================================

void ResetRangedState()
{
	std::lock_guard lock{ g_rangedStateMutex };
	g_recentFireShots.clear();
	g_projectileBindings.clear();
	g_observedProjectileKeys.clear();
}


// ============================================================
// SKSE Messages
// ============================================================

void OnDataLoaded()
{
	if (!ResolveForms()) {
		logger::error(
			"Failed to initialize Magicka Blade forms. Hooks will NOT be installed.");
		return;
	}

	RebuildEnchantmentCache();
	WeaponHitHook::Install();
	ArrowProjectileHook::Install();
	RegisterRangedFireSource();
	RegisterPoisonGuard();

	const bool isAE = REL::Module::IsAE();
	logger::info(
		"Magicka Blade {} active | Runtime={} | MeleePhysicalMutation={} | RangedCore=DUAL_SOURCE_FIRE_GROUPED",
		kInternalVersion,
		isAE ? "AE" : "SE",
		(kEnablePhysicalMutation && !isAE) ? "ON" : "OFF");

	if (auto* console = RE::ConsoleLog::GetSingleton()) {
		console->Print(
			"[tullmagickablade] v1.0.0 native combat active.");
	}
}


void OnPreLoadGame()
{
	{
		std::lock_guard lock{ g_enchantmentCacheMutex };
		g_enchantmentCache.clear();
	}

	ResetRangedState();
}


void OnPostLoadGame()
{
	RebuildEnchantmentCache();
}


void OnNewGame()
{
	ResetRangedState();
	RebuildEnchantmentCache();
}


// ============================================================
// Plugin Entry
// ============================================================

SKSEPluginLoad(
	const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(
		a_skse);


	SetupLog();


	const auto* plugin =
		SKSE::PluginDeclaration::
			GetSingleton();


	if (!plugin) {

		logger::error(
			"Failed to get plugin declaration");

		return false;
	}


	logger::info(
		"{} v{} loaded",
		plugin->GetName(),
		kInternalVersion);


	// HitData call hook uses the trampoline; projectile hooks use the vtable.
	SKSE::AllocTrampoline(
		14);


	const auto* messaging =
		SKSE::
			GetMessagingInterface();


	if (!messaging) {

		logger::error(
			"Failed to get SKSE "
			"messaging interface");

		return false;
	}


	if (!messaging->
			RegisterListener(
				[](
					SKSE::MessagingInterface::
						Message* a_msg)
				{
					if (!a_msg) {
						return;
					}


					switch (a_msg->type)
					{
					case
						SKSE::MessagingInterface::
							kDataLoaded:

						OnDataLoaded();

						break;


					case
						SKSE::MessagingInterface::
							kPreLoadGame:

						OnPreLoadGame();

						break;


					case
						SKSE::MessagingInterface::
							kPostLoadGame:

						OnPostLoadGame();

						break;


					case
						SKSE::MessagingInterface::
							kNewGame:

						OnNewGame();

						break;


					default:

						break;
					}
				})) {

		logger::error(
			"Failed to register "
			"messaging listener");

		return false;
	}


	return true;
}