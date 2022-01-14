import createOpenBw from "./titan.js";
import "./downgrade.js";
import _files from "./list.js";

window.sidegrade = sidegrade;
console.log("empty", _files.some(f => f.trim() === ""));
let openBw;

async function init() {
  console.log("GO")
  openBw = await createOpenBw();
  
  window.openBw = openBw;
  setupCallbacks();

  db_handle = await set_db_handle();
  db_handle.onerror = function (event) {
    console.log("Database error: " + event.target.errorCode);
  };

  try {
    await load_files_from_indexdb();
    callMain();
  $("#select_replay_label").removeClass("disabled");
} catch (e) {
    console.warn(e);
  }


  document
    .getElementById("mpq_files")
    .addEventListener("change", (evt) => {
      console.log("file selected")
      on_mpq_specify_select(evt)
    } );
  document
    .getElementById("select_rep_file")
    .addEventListener("change", on_rep_file_select, false);
  document
    .getElementById("download_rep_file")
    .addEventListener("change", (e) => on_rep_file_select(e, true), false);

  $("#specify_mpqs_button").on("click", function (e) {
    print_to_modal("Specify MPQ files", C_SPECIFY_MPQS_MESSAGE, true);
  });
}

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
var C_FILENAMES = _files;
var C_SPECIFY_MPQS_MESSAGE =
  "BAD BOY";

/*****************************
 * Globals
 *****************************/

var db_handle;
var files = [];
var js_read_buffers = [];
var is_reading = false;

/*****************************
 * Functions
 *****************************/

/**
 * Sets the drop box area depending on whether a replay URL is provided or not.
 * Adds the drag and drop functionality.
 */
jQuery(document).ready(() => {
  init();
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
      console.log(files)
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
  for (var i = 0; i != C_FILENAMES.length; ++i) {
    if (files[i]) {
      var li = document.createElement("li");
      li.appendChild(document.createTextNode(C_FILENAMES[i] + " OK"));
      ul.appendChild(li);
    }
  }

  print_to_modal("Specify MPQ files", status, true);

  if (has_all_files()) {
    console.log("HAS ALL FILES")
    files_to_uint8array_buffers();
    store_files_in_indexdb();

    $("#select_replay_label").removeClass("disabled");
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
      const arr = await sidegrade.convertReplay(srep, chkDowngrader);

      if (download) {
        downloadBlob(arr, `gol-${files[0].name}`);
        return;
      }

      try {
        var buf = openBw.allocate(arr, openBw.ALLOC_NORMAL);
        start_replay(buf, arr.length);
        openBw._free(buf);
      } catch (e) {
        console.error(openBw.getExceptionMessage(e));
      }
    };
  })();
  reader.readAsArrayBuffer(files[0]);
}

function js_fatal_error(ptr) {
  var str = openBw.UTF8ToString(ptr);

  print_to_modal(
    "Fatal error: Unimplemented",
    "Please file a bug report.<br/>" +
      "Only 1v1 replays currently work. Protoss is not supported yet<br/>" +
      "fatal error: " +
      str
  );
}

function print_to_modal(title, text, mpqspecify) {
  $("#rv_modal h3").html(title);
  $("#rv_modal p").html(text);
  // if (mpqspecify) {
  //   $("#mpq_specify").css("display", "inline-block");
  // } else {
  //   $("#mpq_specify").css("display", "none");
  // }

  //@todo open modal
}

function index_by_name(name) {
  for (var i = 0; i != C_FILENAMES.length; ++i) {
    if (C_FILENAMES[i].toLowerCase() == name.toLowerCase()) {
      return i;
    }
  }
  return -1;
}

function has_all_files() {
  for (var i = 0; i != C_FILENAMES.length; ++i) {
    if (!files[i]) {
      console.warn(`missing ${C_FILENAMES[i]}`)
       return false;
    }
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
    js_load_done,
    js_file_index
  );
}

const js_pre_main_loop = () => {};

const js_post_main_loop = () => {
  window.log();
};

const js_read_data = (index, dst, offset, size) => {
  var data = js_read_buffers[index];
  for (var i2 = 0; i2 != size; ++i2) {
    openBw.HEAP8[dst + i2] = data[offset + i2];
  }
};

const filenameFromPath = function (str) {
  return str.split('\\').pop().split('/').pop();
}

const js_file_index = ($0) => {
  console.log(`file index: ${openBw.UTF8ToString($0)}`);
  var filename = filenameFromPath(openBw.UTF8ToString($0));

  var index = files.findIndex(item => 
    filename.toLowerCase() === item.name.toLowerCase());
  console.log(filename, index)
  return index >= 0 ? index : 9999;
}

const js_file_size = (index) => {
  console.log(`file size: ${index} ${files[index].size}`);
  console.log(`file size: ${index} ${js_read_buffers[index].byteLength}`);
  return files[index].size;
};

const js_load_done = () => {
  js_read_buffers = null;
};

/*****************************
 * Database Functions
 *****************************/

function set_db_handle() {
  return new Promise((res, rej) => {
    if (window.indexedDB) {
      var request = window.indexedDB.open("OpenBW_DB", 1);

      request.onerror = function (event) {
        rej("Could not open OpenBW_DB.");
        print_to_modal("Specify MPQ files", C_SPECIFY_MPQS_MESSAGE, true);
      };

      request.onsuccess = (event) => res(event.target.result);

      request.onupgradeneeded = function (event) {
        db_handle = event.target.result;
        var objectStore = db_handle.createObjectStore("mpqs", {
          keyPath: "mpqkp",
        });
        console.log("Database update/create done.");
      };
    } else {
      rej("indexedDB not supported");
    }
  });
}

function load_file_metadata_from_indexdb(store, key, file_index) {
  var request = store.get(key);
  return new Promise((res, rej) => {
    request.onerror = function (event) {
      console.log("Could not retrieve " + key + " from DB.");
      rej("Could not retrieve " + key + " from DB.");
    };
    request.onsuccess = function (event) {
      if (request.result ===  undefined) {
        rej("Result was undefined");
        return;
      }
      console.log(
        "read " +
          key +
          "; size: " +
          request.result.blob.length +
          ": success."
      );
      print_to_modal("Loading MPQs", key + ": success.");
      res(request.result.blob);
    };
  });
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

function store_files_in_indexdb() {
  if (db_handle != null) {
    var transaction = db_handle.transaction(["mpqs"], "readwrite");
    var store = transaction.objectStore("mpqs");

    for (var file_index = 0; file_index < C_FILENAMES.length; file_index++) {
      store.delete(C_FILENAMES[file_index]);
      store_blob(store, C_FILENAMES[file_index], files[file_index]);
    }
  } else {
    console.log("Cannot store MPQs because DB handle is not available.");
  }
}

function load_files_from_indexdb() {
  return new Promise(async (res, rej) => {
    var transaction = db_handle.transaction(["mpqs"]);
    var objectStore = transaction.objectStore("mpqs");
    console.log("attempting to retrieve files from db...");

    try {
      for (var file_index = 0; file_index < C_FILENAMES.length; file_index++) {
        files[file_index] = await load_file_metadata_from_indexdb(objectStore, C_FILENAMES[file_index], file_index);
      }

      console.log("all files loaded from index.");
      files_to_uint8array_buffers().then(res);
    } catch (e) {
      rej("Error while loading MPQs from DB: " + e);
    }
    
  });
}

let _mainCalled = false;
function callMain() {
  try {
    !_mainCalled && openBw.callMain();
    _mainCalled = true;
  } catch (e) {
    console.error(openBw.getExceptionMessage(e));
  }
}

function loop() {
  openBw._next_frame();
  requestAnimationFrame(loop)
}

function start_replay(buffer, length) {
  openBw._load_replay(buffer, length);
  console.log("replay loaded");
  requestAnimationFrame(loop)
}

function files_to_uint8array_buffers() {
  if (is_reading) return;
  return new Promise((res, rej) => {
    is_reading = true;
    var reads_in_progress = C_FILENAMES.length;
    for (var i = 0; i != C_FILENAMES.length; ++i) {
      var reader = new FileReader();
      (function () {
        var index = i;
        reader.onloadend = function (e) {
          if (!e.target.error && e.target.readyState != FileReader.DONE)
            throw "read failed with no error!?";
          if (e.target.error) throw "read failed: " + e.target.error;
          js_read_buffers[index] = new Int8Array(e.target.result);
          --reads_in_progress;

          if (reads_in_progress == 0) {
            res();
          }
        };
      })();
      reader.readAsArrayBuffer(files[i]);
    }
  });
}

let lastUnits = 0;
window.log = () => {
  // console.log(`frame: ${openBw._replay_get_value(2)}`);
  // console.log("units", openBw._counts(0, 1));
  // console.log("units", openBw._counts(0, 1));
  const units = openBw._counts(0, 1);
  if (units != lastUnits) {
    console.log("units", openBw.get_util_funcs().get_units());
    lastUnits = units;
  }
  // console.log("upgrades", openBw._counts(0, 2));
  // console.log("research", openBw._counts(0, 3));
  // console.log("sprite", openBw._counts(0, 4));
  // console.log("image", openBw._counts(0, 5));
  const sounds = openBw._counts(0, 6);
  if (sounds) {
    // console.log(openBw.get_util_funcs().get_sounds());
  }
  // console.log("sound", sounds);
  // console.log("building queue", openBw._counts(0, 7));
  // for (let i = 0; i < 8; ++i) {
  //   console.log("minerals", openBw._counts(i, 8));
  //   console.log("gas", openBw._counts(i, 9));
  //   console.log("workers", openBw._counts(i, 12));
  //   console.log("army", openBw._counts(i, 13));
  // }
  // const ptr = openBw._get_buffer(0)
  // const explored = openBw.HEAPU8[ptr];
  // const visible = openBw.HEAPU8[ptr+ 1];
  // const flags = openBw.HEAPU16[ptr + 1];

  // const explored2 = openBw.HEAPU8[ptr + 100 * 4];
  // const visible2 = openBw.HEAPU8[ptr+ 100 * 4 + 1];
  // const flags2 = openBw.HEAPU16[ptr + 100 * 2 + 1];

  // debugger;
  // const ptr = openBw._get_buffer(1);
  // console.log("unit_id", openBw.HEAPU16[ptr]);
  // console.log("type_id", openBw.HEAP16[ptr+1]);
  // console.log("owner", openBw.HEAP16[ptr+2]);
  // console.log("x", openBw.HEAP16[ptr+3]);
  // console.log("y", openBw.HEAP16[ptr+4]);
  // console.log("hp", openBw.HEAP16[ptr+5]);
  // console.log("sounds", sounds);

  
  // unsigned short int id;
	// short int typeId;
	// short int owner;
	// short int x;
	// short int y;
	// short int hp;
	// short int energy;
	// short int sprite_index = -1;
	// int status_flags;
	// short int direction;
	// short int remainingBuildTime;
	// short int shields; 
	// unsigned char order;
	// unsigned char remainingTrainTime = 0;
	// short int kills;

}