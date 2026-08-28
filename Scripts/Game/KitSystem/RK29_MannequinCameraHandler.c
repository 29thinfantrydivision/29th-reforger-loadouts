//------------------------------------------------------------------------------------------------
//! Hold-drag to turn the mannequin, wheel to zoom; the pump it drives lives in
//! RK29_MannequinView.MannequinCameraFrame. It sits on the workspace, not on the preview widget,
//! and bounds-tests by hand: an ItemPreviewWidget does not reliably raise mouse events, and the
//! bounds test also keeps a wheel over the detail scroll - same band - from zooming an unseen
//! soldier. Events this handler did not take answer false; they belong to widgets it cannot see.
class RK29_MannequinCameraHandler : ScriptedWidgetEventHandler
{
	RK29_MannequinView m_View;

	//------------------------------------------------------------------------------------------------
	override bool OnMouseButtonDown(Widget w, int x, int y, int button)
	{
		if (button != 0 || !m_View || !m_View.CursorOverMannequin(x, y))
			return false;

		m_View.BeginMannequinDrag();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Not bounds-tested, and vanilla's button-up is not either: a drag released anywhere still
	//! ended, and ignoring a release outside the box leaves the soldier spinning with the button up.
	override bool OnMouseButtonUp(Widget w, int x, int y, int button)
	{
		if (button == 0 && m_View)
			m_View.EndMannequinDrag();
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnMouseWheel(Widget w, int x, int y, int wheel)
	{
		if (!m_View || !m_View.CursorOverMannequin(x, y))
			return false;

		m_View.ZoomMannequin(wheel);
		return true;
	}
}
