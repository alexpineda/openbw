import createOpenBw from "./titan.js";
import "./downgrade.js";
let openBw;

async function init() {
  console.log("GO")
  openBw = Object.assign(await createOpenBw(), {
    preRun: [],
    postRun: [],
    locateFile: (path, prefix) => {
      return `${prefix + path}`;
    },
  });

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
var C_FILENAMES = [
  "units.dat", // arr       //0 
  "weapons.dat",            //1  
  "upgrades.dat",           //2
  "techdata.dat",          //3
  "flingy.dat",          //4
  "sprites.dat",         //5
  "images.dat",        //6
  "orders.dat",       //7

  "Melee.trg", // triggers //8

  "badlands.vf4", //Tileset //9
  "badlands.cv5", //10
  "platform.vf4",//11
  "platform.cv5",//12
  "install.vf4", //13
  "install.cv5", //14
  "AshWorld.vf4", //15
  "AshWorld.cv5", //16
  "Jungle.vf4",  //17
  "Jungle.cv5", //18
  "Desert.vf4", //19
  "Desert.cv5", //20
  "Ice.vf4",   //21
  "Ice.cv5",  //22
  "Twilight.vf4", //23
  "Twilight.cv5", //24

  "iscript.bin", // scripts //25



];
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
    parse_mpq_files();
    store_mpq_in_db();

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
      const arr = await sidegrade.sidegradeReplay(srep, chkDowngrader);

      if (download) {
        downloadBlob(arr, `gol-${files[0].name}`);
        return;
      }

      var buf = openBw.allocate(arr, openBw.ALLOC_NORMAL);
      start_replay(buf, arr.length);
      openBw._free(buf);
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
    js_load_done,
    js_file_index
  );
}

const js_pre_main_loop = () => {};

const js_post_main_loop = () => {
  console.log(`frame: ${openBw._replay_get_value(2)}`);
};

const js_read_data = (index, dst, offset, size) => {
  var data = js_read_buffers[index];
  for (var i2 = 0; i2 != size; ++i2) {
    openBw.HEAP8[dst + i2] = data[offset + i2];
  }
};

const js_file_index = ($0) => {
  var filename = openBw.UTF8ToString($0);
  var index = files.indexOf(filename);
  return index >= 0 ? index : 99;
}

const js_file_size = (index) => {
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

function load_file_from_indexdb(store, key, file_index) {
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

function store_mpq_in_db() {
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
        files[file_index] = await load_file_from_indexdb(objectStore, C_FILENAMES[file_index], file_index);
      }

      console.log("all files loaded from index.");
      parse_mpq_files().then(res);
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

function start_replay(buffer, length) {
  openBw._load_replay(buffer, length);
}

function parse_mpq_files() {
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
