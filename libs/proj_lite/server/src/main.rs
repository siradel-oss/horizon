#![feature(proc_macro_hygiene, decl_macro)]

use rocket::*;
use rocket_contrib::{
    database,
    databases::{redis, redis::Commands},
    json::Json,
};
use serde::Serialize;

#[database("epsg_proj")]
struct EpsgProjDbConn(redis::Connection);

#[derive(Serialize)]
struct EpsgResult {
    name: String,
    proj_string: String,
}

#[get("/epsg/<id>")]
fn epsg(conn: EpsgProjDbConn, id: String) -> Option<Json<EpsgResult>> {
    let key_name = format!("crs:EPSG:{}:name", id);
    let key_proj4str = format!("crs:EPSG:{}:proj4str", id);

    let proj_string = conn.0.get(&key_proj4str).ok()?;
    let name = conn.0.get(&key_name).ok()?;

    Some(Json(EpsgResult { name, proj_string }))
}

#[get("/search?<q>&<n>")]
fn search(conn: EpsgProjDbConn, q: String, n: Option<usize>) -> Option<Json<Vec<EpsgResult>>> {
    let n = n.unwrap_or(12).min(50);

    let re = regex::Regex::new("\\w+").unwrap();
    let words: Vec<String> = re
        .find_iter(&q)
        .map(|m| m.as_str().to_lowercase().to_string())
        .collect();

    let word_keys: Vec<String> = words
        .iter()
        .map(|m| format!("search:{}:crs", m.as_str().to_lowercase()))
        .collect();

    let mut to_retrieve = Vec::new();

    for w in &words {
        if w.chars().all(|c| c.is_ascii_digit()) {
            to_retrieve.push(format!("crs:EPSG:{}:name", w));
            to_retrieve.push(format!("crs:EPSG:{}:proj4str", w));
        }
    }

    if !word_keys.is_empty() {
        let keys: Vec<String> = conn.0.sinter(&word_keys[..]).ok()?;
        if !keys.is_empty() {
            for k in keys.iter().take(n) {
                to_retrieve.push(format!("crs:{}:name", k));
                to_retrieve.push(format!("crs:{}:proj4str", k));
            }
        }
    }

    if to_retrieve.is_empty() {
        return Some(Json(Vec::new()));
    }

    let res: Vec<String> = conn.0.get(to_retrieve).ok()?;
    let mut epsg = Vec::new();

    for (i, value) in res.into_iter().enumerate() {
        if i % 2 == 0 {
            epsg.push(EpsgResult {
                name: value,
                proj_string: String::new(),
            })
        } else {
            epsg[i / 2].proj_string = value;
        }
    }

    return Some(Json(epsg));
}

fn main() {
    rocket::ignite()
        .attach(EpsgProjDbConn::fairing())
        .mount("/", routes![epsg, search])
        .launch();
}
