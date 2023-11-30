#include "LagRecords.h"

#include <ranges>

#include "../CFG.h"

void CLagRecords::EraseRecord(C_TFPlayer *pPlayer, int nRecord)
{
	auto &v = m_LagRecords[pPlayer];
	v.erase(v.begin() + nRecord);
}

bool CLagRecords::IsSimulationTimeValid(float flCurSimTime, float flCmprSimTime)
{
	return flCurSimTime - flCmprSimTime < 0.2f;
}

void CLagRecords::AddRecord(C_TFPlayer *pPlayer)
{
	LagRecord_t newRecord = {};

	m_bSettingUpBones = true;

	const auto setup_bones_optimization{ CFG::Misc_SetupBones_Optimization };

	if (setup_bones_optimization)
	{
		pPlayer->InvalidateBoneCache();
	}

	const auto result = pPlayer->SetupBones(newRecord.m_BoneMatrix, 128, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);

	if (setup_bones_optimization)
	{
		for (auto n{ 0 }; n < 32; n++)
		{
			auto attach{ pPlayer->FirstMoveChild() };

			if (!attach)
			{
				break;
			}

			if (attach->ShouldDraw())
			{
				attach->InvalidateBoneCache();
				attach->SetupBones(nullptr, -1, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
			}

			attach = attach->NextMovePeer();
		}
	}

	m_bSettingUpBones = false;

	if (!result)
		return;

	newRecord.m_pPlayer = pPlayer;
	newRecord.m_flSimulationTime = pPlayer->m_flSimulationTime();
	newRecord.m_vAbsOrigin = pPlayer->GetAbsOrigin();
	newRecord.m_vVecOrigin = pPlayer->m_vecOrigin();
	newRecord.m_vAbsAngles = pPlayer->GetAbsAngles();
	newRecord.m_vEyeAngles = pPlayer->GetEyeAngles();
	newRecord.m_vVelocity = pPlayer->m_vecVelocity();
	newRecord.m_vCenter = pPlayer->GetCenter();
	newRecord.m_nFlags = pPlayer->m_fFlags();

	if (const auto pAnimState = pPlayer->GetAnimState())
		newRecord.m_flFeetYaw = pAnimState->m_flCurrentFeetYaw;

	m_LagRecords[pPlayer].emplace_front(newRecord);
}

const LagRecord_t *CLagRecords::GetRecord(C_TFPlayer *pPlayer, int nRecord, bool bSafe)
{
	if (!bSafe)
	{
		if (!m_LagRecords.contains(pPlayer))
			return nullptr;

		if (nRecord < 0 || nRecord > static_cast<int>(m_LagRecords[pPlayer].size() - 1))
			return nullptr;
	}

	return &m_LagRecords[pPlayer][nRecord];
}

bool CLagRecords::HasRecords(C_TFPlayer *pPlayer, int *pTotalRecords)
{
	if (m_LagRecords.contains(pPlayer))
	{
		const size_t nSize = m_LagRecords[pPlayer].size();

		if (nSize <= 0)
			return false;

		if (pTotalRecords)
			*pTotalRecords = static_cast<int>(nSize - 1);

		return true;
	}

	return false;
}

void CLagRecords::UpdateRecords()
{
	const auto pLocal = H::Entities->GetLocal();

	if (!pLocal || pLocal->deadflag() || pLocal->InCond(TF_COND_HALLOWEEN_GHOST_MODE) || pLocal->InCond(TF_COND_HALLOWEEN_KART))
	{
		if (!m_LagRecords.empty())
		{
			m_LagRecords.clear();
		}

		return;
	}

	for (const auto pEntity : H::Entities->GetGroup(CFG::Misc_SetupBones_Optimization ? EEntGroup::PLAYERS_ALL : EEntGroup::PLAYERS_ENEMIES))
	{
		if (!pEntity || pEntity == pLocal)
		{
			continue;
		}

		const auto pPlayer = pEntity->As<C_TFPlayer>();

		if (pPlayer->deadflag())
		{
			m_LagRecords[pPlayer].clear();
		}
	}
	
	for (const auto& records : m_LagRecords | std::views::values)
	{
		for (size_t n = 0; n < records.size(); n++)
		{
			auto& curRecord = records[n];

			if (!curRecord.m_pPlayer || !IsSimulationTimeValid(curRecord.m_pPlayer->m_flSimulationTime(), curRecord.m_flSimulationTime))
			{
				EraseRecord(curRecord.m_pPlayer, n);
			}
		}
	}
}

bool CLagRecords::DiffersFromCurrent(const LagRecord_t *pRecord)
{
	const auto pPlayer = pRecord->m_pPlayer;

	if (!pPlayer)
		return false;

	if (static_cast<int>((pPlayer->m_vecOrigin() - pRecord->m_vAbsOrigin).Length()) != 0)
		return true;

	if (static_cast<int>((pPlayer->GetEyeAngles() - pRecord->m_vEyeAngles).Length()) != 0)
		return true;

	if (pPlayer->m_fFlags() != pRecord->m_nFlags)
		return true;

	if (const auto pAnimState = pPlayer->GetAnimState())
	{
		if (fabsf(pAnimState->m_flCurrentFeetYaw - pRecord->m_flFeetYaw) > 0.0f)
			return true;
	}

	return false;
}

void CLagRecordMatrixHelper::Set(const LagRecord_t *pRecord)
{
	if (!pRecord)
		return;

	const auto pPlayer = pRecord->m_pPlayer;

	if (!pPlayer || pPlayer->deadflag())
		return;

	const auto pCachedBoneData = pPlayer->GetCachedBoneData();

	if (!pCachedBoneData)
		return;

	m_pPlayer = pPlayer;
	m_vAbsOrigin = pPlayer->GetAbsOrigin();
	m_vAbsAngles = pPlayer->GetAbsAngles();
	memcpy(m_BoneMatrix, pCachedBoneData->Base(), sizeof(matrix3x4_t) * pCachedBoneData->Count());

	memcpy(pCachedBoneData->Base(), pRecord->m_BoneMatrix, sizeof(matrix3x4_t) * pCachedBoneData->Count());

	pPlayer->SetAbsOrigin(pRecord->m_vAbsOrigin);
	pPlayer->SetAbsAngles(pRecord->m_vAbsAngles);

	m_bSuccessfullyStored = true;
}

void CLagRecordMatrixHelper::Restore()
{
	if (!m_bSuccessfullyStored || !m_pPlayer)
		return;

	const auto pCachedBoneData = m_pPlayer->GetCachedBoneData();

	if (!pCachedBoneData)
		return;

	m_pPlayer->SetAbsOrigin(m_vAbsOrigin);
	m_pPlayer->SetAbsAngles(m_vAbsAngles);
	memcpy(pCachedBoneData->Base(), m_BoneMatrix, sizeof(matrix3x4_t) * pCachedBoneData->Count());

	m_pPlayer = nullptr;
	m_vAbsOrigin = {};
	m_vAbsAngles = {};
	std::memset(m_BoneMatrix, 0, sizeof(matrix3x4_t) * 128);
	m_bSuccessfullyStored = false;
}