//------------------------------------------------------------------------------------------------
//! Vanilla's horizontal scroller with a speed that can be re-priced at runtime. Its
//! m_fAnimationSpeedPxPerSec is protected and has no setter, so every row moves at the one authored
//! px/sec - which is both too slow to sit through and, once RK29_MarqueeOnHover has shrunk a row's
//! name a step, a different reading rate on that row than on the row above it. The marquee sets a
//! speed from the size the name ended up at each time it starts one. Nothing else is changed: the
//! animation, the states and the wait times are all still vanilla's.
[BaseContainerProps()]
class RK29_ScrollAnimation : SCR_HorizontalScrollAnimationComponent
{
	//------------------------------------------------------------------------------------------------
	//! Safe to call while animating - OnFrame reads the field every frame.
	void SetSpeedPxPerSec(float pxPerSec)
	{
		m_fAnimationSpeedPxPerSec = pxPerSec;
	}
}
