use bevy::prelude::*;

#[derive(Resource, Default, Debug)]
pub struct UiPartyState {
    pub active: bool,
    pub in_lobby: bool,
    pub is_host: bool,
}

#[derive(Resource, Default, Debug)]
pub struct UiMenuDvars {
    values: std::collections::HashMap<String, String>,
}

impl UiMenuDvars {
    pub fn get(&self, name: &str) -> Option<&str> {
        self.values
            .get(&name.to_ascii_lowercase())
            .map(String::as_str)
    }

    pub fn set(&mut self, name: &str, value: impl Into<String>) {
        self.values.insert(name.to_ascii_lowercase(), value.into());
    }

    pub fn iter(&self) -> impl Iterator<Item = (&str, &str)> {
        self.values
            .iter()
            .map(|(name, value)| (name.as_str(), value.as_str()))
    }
}

#[derive(Resource, Default, Debug)]
pub struct FrontendContentLoading;

#[derive(Resource, Clone, Default, Debug)]
pub struct HostMatchRules(pub Vec<(String, String)>);

#[derive(Message, Clone, Debug)]
pub struct UiExecCommand {
    pub text: String,
}

#[derive(Message, Clone, Debug, PartialEq, Eq)]
pub enum UiMenuRequest {
    Toggle,
    RestoreLobby,
    Open(String),
    Close(String),
    Focus { menu: String, item: String },
    Key(UiMenuKey),
    Text(String),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UiMenuKey {
    Escape,
    Enter,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    Backspace,
    Delete,
}

#[derive(Resource, Clone, Copy, Debug, PartialEq, Eq)]
pub struct AssetFamilies(pub u8);

impl AssetFamilies {
    pub const SETTINGS: [(&'static str, &'static str, u8); 3] = [
        ("iw5", "sv_assets_iw5", 2),
        ("t5", "sv_assets_t5", 4),
        ("t6", "sv_assets_t6", 8),
    ];

    pub fn enabled(self, family: &str) -> bool {
        let bit = match family {
            "iw4" => 1,
            "iw5" => 2,
            "t5" => 4,
            "t6" => 8,
            _ => return false,
        };
        self.0 & bit != 0
    }

    pub fn missing_notice(self, have: u8) -> Option<String> {
        let missing = self.0 & !have;
        if missing == 0 {
            return None;
        }
        let names = [
            (1, "Modern Warfare 2 (IW4)"),
            (2, "Modern Warfare 3 (IW5)"),
            (4, "Black Ops (T5)"),
            (8, "Black Ops II (T6)"),
        ]
        .into_iter()
        .filter_map(|(bit, name)| (missing & bit != 0).then_some(name))
        .collect::<Vec<_>>()
        .join(", ");
        let names = if missing & !15 != 0 {
            if names.is_empty() {
                "unknown game content (update IW4L)".to_owned()
            } else {
                format!("{names}, unknown game content (update IW4L)")
            }
        } else {
            names
        };
        Some(format!(
            "Missing game files:\n{names}.\n\nInstall these games and select their game folders,\nor ask the host to disable their optional asset families."
        ))
    }

    pub fn from_inventory(have: u8, dvars: &UiMenuDvars) -> Result<Self, String> {
        let mut enabled = have & 1;
        for (family, setting, bit) in Self::SETTINGS {
            let env = std::env::var(format!("IW4L_ASSETS_{}", family.to_ascii_uppercase())).ok();
            let value = dvars.get(setting).or(env.as_deref());
            let allow = match value {
                None => have & bit != 0,
                Some("0") => false,
                Some("1") => true,
                Some(_) => return Err(format!("{setting} must be 0 or 1")),
            };
            if allow {
                if have & bit == 0 {
                    return Err(format!(
                        "Cannot enable {family}: game content is not installed"
                    ));
                }
                enabled |= bit;
            }
        }
        Ok(Self(enabled))
    }

    pub fn require_map(self, map: &str) -> Result<(), String> {
        let family = map.split_once(':').map_or("iw4", |(family, _)| family);
        if self.enabled(family) {
            Ok(())
        } else {
            Err(format!(
                "Map `{map}` requires disabled or missing {family} content"
            ))
        }
    }
}
