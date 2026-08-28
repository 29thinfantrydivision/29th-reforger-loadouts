//------------------------------------------------------------------------------------------------
//! PIP sights whose rungs are solved against the chambered round, not the authored angles.
//! DO NOT let an empty chamber snap back to the authored angle: firing would jar the picture.
//! Do not mix with generator-authored ladders on one weapon - BallisticTable is the coarser AI
//! table and reads ~0.23 deg high at 100 m, where its rows are ~0.64 s apart.
//------------------------------------------------------------------------------------------------

[EntityEditorProps(category: "GameScripted/Weapon/Sights", description: "PIP sights ranged against the chambered round.", color: "0 0 255 255")]
class RK29_LauncherSightsComponentClass : RHS_2DPIPSightsComponentClass
{
}

//------------------------------------------------------------------------------------------------
class RK29_LauncherSightsComponent : RHS_2DPIPSightsComponent
{
	// Last solved rung. Doubles as the hold across an empty chamber, so a fired shot leaves the
	// sight picture where it was rather than snapping to the authored fallback.
	protected float m_fSolvedDistance = -1;
	protected float m_fSolvedPitch;

	//------------------------------------------------------------------------------------------------
	protected override float GetCameraPitchTarget()
	{
		float authored = super.GetCameraPitchTarget();

		SCR_2DOpticsComponentClass data = SCR_2DOpticsComponentClass.Cast(GetComponentData(GetOwner()));
		if (!data || data.GetZeroType() != SCR_EPIPZeroingType.EPZ_CAMERA_TURN)
			return authored;

		float distance = GetCurrentSightsRange()[1];
		if (distance <= 0)
			return authored;

		float pitch;
		if (SolvePitch(distance, pitch))
		{
			m_fSolvedDistance = distance;
			m_fSolvedPitch = pitch;
			return pitch;
		}

		// Nothing chambered, or past the round's max range: hold this rung's last answer.
		if (distance == m_fSolvedDistance)
			return m_fSolvedPitch;

		return authored;
	}

	//------------------------------------------------------------------------------------------------
	//! Superelevation for one rung, as a camera pitch. False when the ballistics cannot answer.
	protected bool SolvePitch(float distance, out float outPitch)
	{
		BaseMuzzleComponent muzzle = CurrentMuzzle();
		if (!muzzle)
			return false;

		float travelTime;
		float aimHeight = BallisticTable.GetAimHeightOfNextProjectile(distance, travelTime, muzzle, true);

		// Past the round's max range the engine hands back a negative time and the height of that
		// max range, which would read as a valid but wrong angle. An empty chamber answers the same.
		if (travelTime <= 0)
			return false;

		float angle = Math.Atan2(aimHeight, distance);
		if (angle <= 0)
			return false;

		// Negative like the authored branch: the camera pitches DOWN by the superelevation, and
		// putting the reticle back on the target is what raises the tube.
		outPitch = -angle * Math.RAD2DEG;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! The weapon this glass is bolted to. Walks parents rather than assuming the optic hangs
	//! straight off the launcher: an attachment can sit on another attachment's rail, which is the
	//! same walk SCR_2DOpticsComponent.GetCameraLocalTransform does to find its weapon root.
	protected BaseMuzzleComponent CurrentMuzzle()
	{
		IEntity node = GetOwner();
		while (node)
		{
			BaseWeaponComponent weapon = BaseWeaponComponent.Cast(node.FindComponent(BaseWeaponComponent));
			if (weapon)
				return weapon.GetCurrentMuzzle();

			node = node.GetParent();
		}

		return null;
	}
}
