# Server asset families

Hosts select optional IW5, T5 and T6 assets in the private lobby's
Game Setup → Game Assets submenu, before making the lobby public. Missing installations cannot be enabled. Installed
families start enabled. Console settings are `set sv_assets_iw5 0|1`,
`set sv_assets_t5 0|1` and `set sv_assets_t6 0|1`; launch defaults can be set with
`IW4L_ASSETS_IW5`, `IW4L_ASSETS_T5` and `IW4L_ASSETS_T6` (0 or 1).
IW4 remains required. A map from a disabled family cannot be selected or loaded.

A public lobby fixes its enabled families when it is created. Return to private
before changing them. Joining requires every enabled family, and all members
load exactly that selection, including weapons, sounds and materials. Disabled
families are excluded even when installed locally; cache reuse includes the
selection. Disabling IW5 and T5 lets players without those games join, provided
they have IW4 and every other enabled family.

Saved classes using disabled weapons remain unavailable; select or edit a class
using enabled families before spawning.

If required game files are missing, joining is blocked with a menu notice naming
the games to install. The host can instead disable those optional families.
