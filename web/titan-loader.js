import createOpenBw from "./titan.js";
import "./downgrade.js";
let openBw;

createOpenBw().then(function (_openBw) {
   openBw = Object.assign(_openBw, {
    preRun: [],
    postRun: [],
    locateFile: (path, prefix) => {
      return `${prefix + path}`;
    },
  });

  window.openBw = openBw;
  setupCallbacks();
});
/*
 * Replay value mappings: _replay_get_value(x):
 *
 * 0: game speed [2^n], n in [-oo, 8192]
 * 1: paused [0, 1]
 * 2: current frame that is being displayed (integer)
 * 3: target frame to which the replay viewer will fast-forward (integer)
 * 4: end frame (integer)
 * 5: map name (string)
 * 6: percentage of frame / end frame used to position the slider handle [0..1] (double)
 */

/*****************************
 * Constants
 *****************************/
var C_PLAYER_ACTIVE = 0;
var C_USED_ZERG_SUPPLY = 3;
var C_AVAILABLE_ZERG_SUPPLY = 6;
var C_RACE = 13;

var C_MPQ_FILENAMES = ["StarDat.mpq", "BrooDat.mpq", "Patch_rt.mpq"];
var C_SPECIFY_MPQS_MESSAGE =
  "Please select StarDat.mpq, BrooDat.mpq and patch_rt.mpq from your StarCraft directory.";

/*****************************
 * Globals
 *****************************/


var db_handle;
var main_has_been_called = false;
var load_replay_data_arr = null;

var files = [];
var js_read_buffers = [];
var is_reading = false;
var players = [];

/*****************************
 * Functions
 *****************************/

/**
 * Sets the drop box area depending on whether a replay URL is provided or not.
 * Adds the drag and drop functionality.
 */
jQuery(document).ready(function ($) {

  set_db_handle(function (event) {
    db_handle = event.target.result;
    db_handle.onerror = function (event) {
      // Generic error handler for all errors targeted at this database's requests!
      console.log("Database error: " + event.target.errorCode);
    };

    // the db_handle has successfully been obtained. Now attempt to load the MPQs.
    load_mpq_from_db();
  });

  document
    .getElementById("mpq_files")
    .addEventListener("change", on_mpq_specify_select, false);
  document
    .getElementById("select_rep_file")
    .addEventListener("change", on_rep_file_select, false);
  document
    .getElementById("download_rep_file")
    .addEventListener("change", (e) => on_rep_file_select(e, true), false);

  $("#specify_mpqs_button").on("click", function (e) {
    print_to_modal("Specify MPQ files", C_SPECIFY_MPQS_MESSAGE, true);
  });
});


//  var funcs = Module.get_util_funcs();
//    update_production_tab(funcs.get_all_incomplete_units());
//    update_army_tab(funcs.get_all_completed_units());
//        funcs.get_completed_upgrades(players[i]),
//        funcs.get_incomplete_upgrades(players[i]),

//  update_handle_position(_replay_get_value(6) * 200);
//  update_timer(_replay_get_value(2));
//  update_speed(_replay_get_value(0));

//  var array_index = Math.round(frame / 16);
//  if (array_index >= resource_count[0].length) {
//    resource_count[0].length = array_index + 1;
//  }
//  resource_count[0][array_index] = _replay_get_value(2);

//  set_map_name(UTF8ToString(_replay_get_value(5)));
//  set_nick(i + 1, UTF8ToString(_player_get_value(players[i], C_NICK)));
//  set_apm(i + 1, _player_get_value(players[i], C_APM));

/*****************************
 * Listener functions
 *****************************/

function on_rep_file_select(e, download = false) {
  var input_files = e.target.files;
  load_replay_file(input_files, download);
}

function on_mpq_specify_select(e) {
  var input_files = e.target.files;

  var unrecognized_files = 0;
  for (var i = 0; i != input_files.length; ++i) {
    var index = index_by_name(input_files[i].name);
    if (index != -1) {
      files[index] = input_files[i];
    } else {
      ++unrecognized_files;
    }
  }

  var status = "";
  if (has_all_files()) {
    status = "Loading, please wait...";
  } else if (unrecognized_files != 0) {
    status = C_SPECIFY_MPQS_MESSAGE + "<br/>Unrecognized files selected";
  } else {
    status = C_SPECIFY_MPQS_MESSAGE;
  }

  var ul = document.getElementById("list");
  while (ul.firstChild) ul.removeChild(ul.firstChild);
  for (var i = 0; i != C_MPQ_FILENAMES.length; ++i) {
    if (files[i]) {
      var li = document.createElement("li");
      li.appendChild(document.createTextNode(C_MPQ_FILENAMES[i] + " OK"));
      ul.appendChild(li);
    }
  }

  print_to_modal("Specify MPQ files", status, true);

  if (has_all_files()) {
    parse_mpq_files();
    store_mpq_in_db();

    $("#play_demo_button").removeClass("disabled");
    $("#select_replay_label").removeClass("disabled");
    close_modal();
  }
}

/*****************************
 * Helper functions
 *****************************/
const downloadURL = (data, fileName) => {
  const a = document.createElement("a");
  a.href = data;
  a.download = fileName;
  a.textContent = "download";
  document.body.appendChild(a);
  a.style.display = "none";
  a.click();
  a.remove();
};

const downloadBlob = (data, fileName) => {
  const blob = new Blob([data]);
  const url = window.URL.createObjectURL(blob);
  downloadURL(url, fileName);
  setTimeout(() => window.URL.revokeObjectURL(url), 1000);
};

function load_replay_file(files, download) {
  if (files.length != 1) return;
  var reader = new FileReader();
  (function () {
    reader.onloadend = async function (e) {
      if (!e.target.error && e.target.readyState != FileReader.DONE)
        throw "read failed with no error!?";
      if (e.target.error) throw "read failed: " + e.target.error;
      var og = new Int8Array(e.target.result);
      const srep = await sidegrade.parseReplay(og);
      const chkDowngrader = new sidegrade.ChkDowngrader();
      const arr = await sidegrade.sidegradeReplay(srep, chkDowngrader);

      if (download) {
        downloadBlob(arr, `gol-${files[0].name}`);
        return;
      }

      if (main_has_been_called) {
        var buf = openBw.allocate(arr, openBw.ALLOC_NORMAL);
        start_replay(buf, arr.length);
        openBw._free(buf);
      } else {
        load_replay_data_arr = arr;
        if (has_all_files()) {
          on_read_all_done();
        }
      }
    };
  })();
  reader.readAsArrayBuffer(files[0]);
}

function js_fatal_error(ptr) {
  var str = UTF8ToString(ptr);

  print_to_modal(
    "Fatal error: Unimplemented",
    "Please file a bug report.<br/>" +
      "Only 1v1 replays currently work. Protoss is not supported yet<br/>" +
      "fatal error: " +
      str
  );
}

const print_to_canvas = (...args) => console.log(...args);

function print_to_modal(title, text, mpqspecify) {
  $("#rv_modal h3").html(title);
  $("#rv_modal p").html(text);
  if (mpqspecify) {
    $("#mpq_specify").css("display", "inline-block");
  } else {
    $("#mpq_specify").css("display", "none");
  }

  //@todo open modal
}

function close_modal() {
  //@todo close modal
}

function index_by_name(name) {
  for (var i = 0; i != C_MPQ_FILENAMES.length; ++i) {
    if (C_MPQ_FILENAMES[i].toLowerCase() == name.toLowerCase()) {
      return i;
    }
  }
  return -1;
}

function has_all_files() {
  for (var i = 0; i != C_MPQ_FILENAMES.length; ++i) {
    if (!files[i]) return false;
  }
  return true;
}

/*****************************
 * Callback functions
 *****************************/

function setupCallbacks() {
  openBw.setupCallbacks(
    js_fatal_error,
     js_pre_main_loop, 
     js_post_main_loop, 
     js_file_size, 
     js_read_data,
     js_load_done
  )
};

const js_pre_main_loop = () => {}

const js_post_main_loop = () => {
  console.log(`frame: ${openBw._replay_get_value(2)}`);
}

const js_read_data =  (index, dst, offset, size) => {
  var data = js_read_buffers[index];
  for (var i2 = 0; i2 != size; ++i2) {
    openBw.HEAP8[dst + i2] = data[offset + i2];
  }
}

const js_file_size= (index) => {
  return files[index].size;
}

const js_load_done = () => {
  js_read_buffers = null;
}

/*****************************
 * Database Functions
 *****************************/

function set_db_handle(success_callback) {
  if (window.indexedDB) {
    var request = window.indexedDB.open("OpenBW_DB", 1);

    request.onerror = function (event) {
      console.log("Could not open OpenBW_DB.");
      print_to_modal("Specify MPQ files", C_SPECIFY_MPQS_MESSAGE, true);
    };

    request.onsuccess = success_callback;

    request.onupgradeneeded = function (event) {
      db_handle = event.target.result;
      var objectStore = db_handle.createObjectStore("mpqs", {
        keyPath: "mpqkp",
      });
      console.log("Database update/create done.");
    };
  } else {
    console.log("indexedDB not supported.");
  }
}

function get_blob(store, key, file_index, callback) {
  var request = store.get(key);
  request.onerror = function (event) {
    console.log("Could not retrieve " + key + " from DB.");
    print_to_modal("Loading MPQs", key + ": failed.");
  };
  request.onsuccess = function (event) {
    files[file_index] = request.result.blob;
    console.log(
      "read " +
        request.result.mpqkp +
        "; size: " +
        request.result.blob.length +
        ": success."
    );
    print_to_modal("Loading MPQs", key + ": success.");
    callback(file_index);
  };
}

function store_blob(store, key, file) {
  console.log("Attempting to store " + key);
  var obj = { mpqkp: key };
  obj.blob = file;

  var request = store.add(obj);
  request.onerror = function (event) {
    console.log("Could not store " + key + " to DB.");
  };
  request.onsuccess = function (evt) {
    console.log("Storing " + key + ": successful.");
  };
}

function store_mpq_in_db() {
  if (db_handle != null) {
    var transaction = db_handle.transaction(["mpqs"], "readwrite");
    var store = transaction.objectStore("mpqs");

    for (var file_index = 0; file_index < 3; file_index++) {
      store.delete(C_MPQ_FILENAMES[file_index]);
      store_blob(store, C_MPQ_FILENAMES[file_index], files[file_index]);
    }
  } else {
    console.log("Cannot store MPQs because DB handle is not available.");
  }
}

function load_mpq_from_db() {
  var transaction = db_handle.transaction(["mpqs"]);
  var objectStore = transaction.objectStore("mpqs");
  console.log("attempting to retrieve files from db...");

  var callback = function (index) {
    if (index == 2) {
      if (has_all_files()) {
        console.log("all files read.");
        close_modal();
        parse_mpq_files();
      } else {
        print_to_modal("Specify MPQ files", C_SPECIFY_MPQS_MESSAGE, true);
      }
    }
  };

  for (var file_index = 0; file_index < 3; file_index++) {
    get_blob(objectStore, C_MPQ_FILENAMES[file_index], file_index, callback);
  }
}

/*****************************
 * Other
 *****************************/

function load_replay_url(url) {
  print_to_modal("Status", "Downloading " + url + "...");

  var req = new XMLHttpRequest();
  req.onreadystatechange = function () {
    if (req.readyState == XMLHttpRequest.DONE && req.status == 200) {
      var arr = new Int8Array(req.response);
      var buf = openBw.allocate(arr, openBw.ALLOC_NORMAL);
      start_replay(buf, arr.length);
      openBw._free(buf);
    } else {
      print_to_modal("Status", "fetching " + url + ": " + req.statusText);
    }
  };
  req.responseType = "arraybuffer";
  req.open("GET", url, true);
  req.send();
}

function start_replay(buffer, length) {
  close_modal();

  if (!main_has_been_called) {
    try {
      openBw.callMain();
    } catch (e) {
      console.error(openBw.getExceptionMessage(e));
    }
    main_has_been_called = true;
  }

  openBw._load_replay(buffer, length);

  players = [];
  for (var i = 0; i != 12; ++i) {
    if (openBw._player_get_value(i, C_PLAYER_ACTIVE)) {
      var race = openBw._player_get_value(i, C_RACE);
      var used_supply = openBw._player_get_value(i, C_USED_ZERG_SUPPLY + race);
      var available_supply = openBw._player_get_value(
        i,
        C_AVAILABLE_ZERG_SUPPLY + race
      );

      if (used_supply == 4 && available_supply > 0) {
        players.push(i);
      }
    }
  }

}

function on_read_all_done() {
  // if a replay is specified, then run it. else do nothing

  if (load_replay_data_arr) {
    var arr = load_replay_data_arr;
    load_replay_data_arr = null;
    var buf = openBw.allocate(arr, openBw.ALLOC_NORMAL);
    start_replay(buf, arr.length);
    openBw._free(buf);
  } else {
    var inputs = {};
    var optstr = document.location.search.substr(1);
    if (optstr) {
      var s = optstr.split("&");
      for (var i = 0; i != s.length; ++i) {
        var str = s[i];
        var t = str.split("=");
        if (t[0] && t[1]) {
          inputs[decodeURIComponent(t[0])] = decodeURIComponent(t[1]);
        }
      }
    }
    console.log("inputs", inputs);
    if (inputs.url) {
      load_replay_url(inputs.url);
    } else if (ajax_object.replay_file != null) {
      load_replay_url(ajax_object.replay_file);
    } else {
      $("#select_replay_label").removeClass("disabled");
    }
  }
}

function parse_mpq_files() {
  if (is_reading) return;
  is_reading = true;
  var reads_in_progress = 3;
  for (var i = 0; i != 3; ++i) {
    var reader = new FileReader();
    (function () {
      var index = i;
      reader.onloadend = function (e) {
        if (!e.target.error && e.target.readyState != FileReader.DONE)
          throw "read failed with no error!?";
        if (e.target.error) throw "read failed: " + e.target.error;
        js_read_buffers[index] = new Int8Array(e.target.result);
        --reads_in_progress;

        if (reads_in_progress == 0) on_read_all_done();
      };
    })();
    reader.readAsArrayBuffer(files[i]);
  }
}
