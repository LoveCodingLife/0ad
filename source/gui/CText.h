/*
GUI Object - Text [field]

--Overview--

	GUI Object representing a text field

--More info--

	Check GUI.h

*/

#ifndef INCLUDED_CTEXT
#define INCLUDED_CTEXT

//--------------------------------------------------------
//  Includes / Compiler directives
//--------------------------------------------------------
#include "GUI.h"

// TODO Gee: Remove
class IGUIScrollBar;

//--------------------------------------------------------
//  Macros
//--------------------------------------------------------

//--------------------------------------------------------
//  Types
//--------------------------------------------------------

//--------------------------------------------------------
//  Declarations
//--------------------------------------------------------

/**
 * Text field that just displays static text.
 * 
 * @see IGUIObject
 */
class CText : public IGUIScrollBarOwner, public IGUITextOwner
{
	GUI_OBJECT(CText)

public:
	CText();
	virtual ~CText();

	virtual void ResetStates() { IGUIScrollBarOwner::ResetStates(); }

protected:
	/**
	 * Sets up text, should be called every time changes has been
	 * made that can change the visual.
	 */
	void SetupText();

	/**
	 * Handle Messages
	 *
	 * @param Message GUI Message
	 */
	virtual void HandleMessage(const SGUIMessage &Message);

	/**
	 * Draws the Text
	 */
	virtual void Draw();

	/**
	 * Placement of text. Ignored when scrollbars are active.
	 */
	CPos m_TextPos;

};

#endif
