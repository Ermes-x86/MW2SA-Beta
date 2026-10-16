use super::*;
use std::sync::atomic::AtomicBool;

#[derive(Resource)]
pub struct CommunityPlayerCounts {
    counts: Arc<Mutex<Vec<Option<u32>>>>,
    online: Arc<AtomicBool>,
    cancel: CancellationToken,
}

impl CommunityPlayerCounts {
    pub fn snapshot(&self) -> Option<Vec<Option<u32>>> {
        self.counts.try_lock().ok().map(|counts| counts.clone())
    }
}

impl Drop for CommunityPlayerCounts {
    fn drop(&mut self) {
        self.cancel.cancel();
    }
}

pub(super) fn poll_communities(
    mut commands: Commands,
    counts: Option<Res<CommunityPlayerCounts>>,
    bridge: Option<Res<MasterBridge>>,
) {
    let online = bridge.as_ref().is_none_or(|bridge| {
        !matches!(
            bridge.state(),
            MasterBridgeState::Hosting { .. } | MasterBridgeState::Joined { .. }
        )
    });
    if let Some(counts) = counts {
        counts.online.store(online, Ordering::Relaxed);
        return;
    }
    let counts = CommunityPlayerCounts {
        counts: Arc::new(Mutex::new(vec![None; updater::communities().len()])),
        online: Arc::new(AtomicBool::new(online)),
        cancel: CancellationToken::new(),
    };
    for (index, entry) in updater::communities().iter().enumerate() {
        let community = std::fs::read(&entry.path)
            .ok()
            .and_then(|bytes| updater::Community::parse(&bytes).ok());
        let Some(community) = community else {
            continue;
        };
        let target = MasterTarget {
            address: community.master.address,
            server_name: community.master.server_name,
            ca_pem: community.updates.ca_pem,
        };
        let selected = Some(entry.path.as_path()) == updater::selected_path();
        let state = Arc::clone(&counts.counts);
        let online = Arc::clone(&counts.online);
        let cancel = counts.cancel.clone();
        std::thread::Builder::new()
            .name(format!("iw4l-population-{index}"))
            .spawn(move || worker(target, index, selected, state, online, cancel))
            .expect("spawn community population worker");
    }
    commands.insert_resource(counts);
}

fn worker(
    target: MasterTarget,
    index: usize,
    selected: bool,
    state: Arc<Mutex<Vec<Option<u32>>>>,
    online: Arc<AtomicBool>,
    cancel: CancellationToken,
) {
    let Ok(runtime) = blocking_runtime() else {
        return;
    };
    runtime.block_on(async {
        while !cancel.is_cancelled() {
            let result: Result<()> = async {
                let (_endpoint, connection) = connect(&target, &cancel).await?;
                let (mut send, mut recv) = io_timeout(&cancel, connection.open_bi()).await?;
                write_frame(
                    &mut send,
                    &ControlFrame::Hello(ControlHello {
                        protocol_version: master_protocol::PROTOCOL_VERSION,
                        game_protocol: crate::PROTOCOL_VERSION,
                        role: EndpointRole::Cli,
                        build: endpoint_build(),
                        player_name: String::new(),
                    }),
                )
                .await?;
                let mut request_id = 0;
                loop {
                    let present = selected && online.load(Ordering::Relaxed);
                    request_id += 1;
                    let players = io_timeout(&cancel, async {
                        write_frame(
                            &mut send,
                            &ControlFrame::Request(ControlRequest {
                                request_id,
                                body: RequestBody::Population { online: present },
                            }),
                        )
                        .await?;
                        match read_frame(&mut recv).await? {
                            ControlFrame::Response(ControlResponse {
                                request_id: id,
                                body: ResponseBody::Population { players },
                            }) if id == request_id => Result::<u32>::Ok(players),
                            frame => {
                                Err(format!("unexpected population response {frame:?}").into())
                            }
                        }
                    })
                    .await?;
                    state.lock().expect("community counts poisoned")[index] = Some(players);
                    for _ in 0..50 {
                        tokio::select! {
                            _ = cancel.cancelled() => return Ok(()),
                            _ = tokio::time::sleep(Duration::from_millis(100)) => {},
                        }
                        if present != (selected && online.load(Ordering::Relaxed)) {
                            break;
                        }
                    }
                }
            }
            .await;
            state.lock().expect("community counts poisoned")[index] = None;
            if result.is_err() {
                tokio::select! {
                    _ = cancel.cancelled() => return,
                    _ = tokio::time::sleep(Duration::from_secs(5)) => {},
                }
            }
        }
    });
}
