var infoChart;

function toggle_graphs(tab_nr) {
  if ($("#graphs_tab").is(":visible")) {
    if ($("#graphs_tab_panel" + tab_nr).hasClass("is-active")) {
      $("#graphs_tab").toggle();
    } else {
      $("#graphs_link" + tab_nr).click();
    }
  } else {
    $("#graphs_tab").toggle();
    $("#graphs_link" + tab_nr).click();
  }
}

function toggle_info_tab(tab_nr) {
  if ($("#info_tab").is(":visible")) {
    if ($("#info_tab_panel" + tab_nr).hasClass("is-active")) {
      $("#info_tab").toggle();
    } else {
      $("#tab_link" + tab_nr).click();
    }
  } else {
    $("#info_tab").toggle();
    $("#tab_link" + tab_nr).click();
  }
  if (main_has_been_called) {
    update_info_tab();
  }
}

function jump_back(seconds) {
  var frame = Math.max(0, _replay_get_value(2) - 24 * seconds);
  _replay_set_value(3, frame);
}

function play_faster() {
  var current_speed = _replay_get_value(0);
  if (current_speed < 1024) {
    _replay_set_value(0, current_speed * 2);
    update_speed(current_speed * 2);
  }
}

function play_slower() {
  var current_speed = _replay_get_value(0);
  _replay_set_value(0, current_speed / 2);
  update_speed(current_speed / 2);
}

var volume_index;
function toggle_sound() {
  $("#rv-rc-sound").toggleClass("rv-rc-sound");
  $("#rv-rc-sound").toggleClass("rv-rc-muted");
}

function toggle_pause() {
  $("#rv-rc-play").toggleClass("rv-rc-play");
  $("#rv-rc-play").toggleClass("rv-rc-pause");

  update_info_tab();

  _replay_set_value(1, (_replay_get_value(1) + 1) % 2);
}

function update_speed(speed) {
  document.getElementById("rv-rc-speed").innerHTML = "speed: " + speed + "x";
}

var IMG_URL1 = "http://www.openbw.com/bw/production_icons/icon ";
var IMG_URL2 = ".bmp";
function set_icon(tab_nr, parent_element, child_nr, icon_id, percentage, info) {
  if (icon_id < 10) icon_id = "0" + icon_id;
  if (icon_id < 100) icon_id = "0" + icon_id;

  var img_src = IMG_URL1 + icon_id + IMG_URL2;
  var element = parent_element.children("div").eq(child_nr);
  var img_element = element.children("img");

  if (img_element.attr("src").localeCompare(img_src) != 0) {
    img_element.attr("src", img_src);
  }
  if (tab_nr == 2) {
    element.children("div").html(info);
  } else {
    element.children("div").css("width", Math.round(percentage * 36) + "px");
  }
  if (tab_nr == 3) {
    element.children("span").html(info);
  }
  element.css("display", "inline-block");
}

function clear_icon(parent_element, child_nr) {
  var element = parent_element.children("div").eq(child_nr).hide();
}

function update_army_tab(complete_units) {
  var unit_types = [[], [], [], [], [], [], [], [], [], [], [], []];
  for (var i = 0; i != complete_units.length; ++i) {
    var unit = complete_units[i];
    var type = unit.unit_type().id;
    if (type < 106 && type != 7 && type != 41 && type != 64) {
      // tank siege mode hack (assign id for tank tank mode)
      if (type == 30) {
        type = 5;
      }

      if (type in unit_types[unit.owner]) {
        unit_types[unit.owner][type] += 1;
      } else {
        unit_types[unit.owner][type] = 1;
      }
    }
  }

  var element;
  for (var i = 0; i < players.length; ++i) {
    var type_count = 0;
    element = $("#army_tab_content" + (i + 1));
    for (type in unit_types[players[i]]) {
      var count = unit_types[players[i]][type];

      set_icon(2, element, type_count, type, 1, count);
      ++type_count;
    }
    for (var j = type_count; j < 20; j++) {
      clear_icon(element, j);
    }
  }
}

var relevant_research = [
  0,
  1,
  2,
  3,
  5,
  7,
  8,
  9,
  10,
  11,
  13,
  15,
  16,
  17,
  19,
  20,
  21,
  22,
  24,
  25,
  27,
  30,
  31,
  32,
];
var unused_research = [4, 6, 12, 14, 18, 23, 26, 28, 29, 33, 34];

function update_research_tab(researches) {
  var element;
  for (var i = 0; i < researches.length; i++) {
    element = $("#research_tab_content" + (i + 1));
    var upgrade_count = 1;
    var complete = researches[i][1];
    var index = 0;
    for (var j = 0; j < complete.length; j++) {
      if ($.inArray(complete[j].id, unused_research) == -1) {
        set_icon(4, element, index, complete[j].icon, 1, null);
        index++;
      }
    }

    var incomplete = researches[i][2];
    for (var j = 0; j < incomplete.length; j++) {
      var build_percentage =
        1 - incomplete[j].remaining_time / incomplete[j].total_time;
      set_icon(
        4,
        element,
        j + index,
        incomplete[j].icon,
        build_percentage,
        null
      );
    }

    //clear the unused spots
    for (var j = incomplete.length + index; j < 20; ++j) {
      clear_icon(element, j);
    }
  }
}

function update_upgrades_tab(upgrades) {
  var element;
  for (var i = 0; i < upgrades.length; i++) {
    var upgrade_count = 1;
    var complete = upgrades[i][1];
    element = $("#upgrade_tab_content" + (i + 1));

    for (var j = 0; j < complete.length; j++) {
      set_icon(3, element, j, complete[j].icon, 1, complete[j].level);
    }

    var incomplete = upgrades[i][2];
    for (var j = 0; j < incomplete.length; j++) {
      var build_percentage =
        1 - incomplete[j].remaining_time / incomplete[j].total_time;
      set_icon(
        3,
        element,
        j + complete.length,
        incomplete[j].icon,
        build_percentage,
        incomplete[j].level
      );
    }

    //clear the unused spots
    for (var j = complete.length + incomplete.length; j < 20; ++j) {
      clear_icon(element, j);
    }
  }
}

var productionUnit_compare = function (unit1, unit2) {
  var build_time1 = unit1.build_type()
    ? unit1.build_type().build_time
    : unit1.unit_type().build_time;
  var build_time2 = unit2.build_type()
    ? unit2.build_type().build_time
    : unit2.unit_type().build_time;

  return (
    build_time2 -
    unit2.remaining_build_time -
    (build_time1 - unit1.remaining_build_time)
  );
};

function update_production_tab(incomplete_units) {
  incomplete_units.sort(productionUnit_compare);

  var unit_names = [[], [], [], [], [], [], [], [], [], [], [], []];

  for (var i = 0; i != incomplete_units.length; ++i) {
    var u = incomplete_units[i];
    var t;
    var build_time;
    if (u.build_type()) {
      t = u.build_type().id;
      build_time = u.build_type().build_time;
    } else {
      t = u.unit_type().id;
      build_time = u.unit_type().build_time;
    }

    var build_percentage = 1 - u.remaining_build_time / build_time;

    unit_names[u.owner].push([t, build_percentage]);
  }

  var element;
  for (var i = 0; i < players.length; ++i) {
    element = $("#production_tab_content" + (i + 1));

    //fill the spots with all units in production for current player
    for (var j = 0; j != unit_names[players[i]].length; ++j) {
      set_icon(
        1,
        element,
        j,
        unit_names[players[i]][j][0],
        unit_names[players[i]][j][1],
        null
      );
    }

    //clear the unused spots
    for (var j = unit_names[players[i]].length; j < 100; ++j) {
      clear_icon(element, j);
    }
  }
}

function update_timer(sec_num) {
  sec_num = (sec_num * 42) / 1000;
  var hours = Math.floor(sec_num / 3600);
  var minutes = Math.floor((sec_num - hours * 3600) / 60);
  var seconds = Math.floor(sec_num - hours * 3600 - minutes * 60);

  if (hours < 10) {
    hours = "0" + hours;
  }
  if (minutes < 10) {
    minutes = "0" + minutes;
  }
  if (seconds < 10) {
    seconds = "0" + seconds;
  }

  var time = minutes + ":" + seconds;
  if (hours > 0) {
    time = hours + ":" + time;
  }
  document.getElementById("rv-rc-timer").innerHTML = "time: " + time;
}

var isDown = false;
var isClicked = false;

function update_handle_position(value) {
  if (!isDown && !isClicked) {
    document.getElementById("sliderOutput").value = value;
    $("#sliderOutput").trigger("change");
  }
}

function set_map_name(name) {
  document.getElementById("map1").innerHTML = name;
  document.getElementById("map2").innerHTML = name;
}

function set_color(player, color) {
  var rgb_color;
  switch (color) {
    case 0:
      rgb_color = "rgba(244, 4, 4, 1)";
      break;
    case 1:
      rgb_color = "rgba(12, 72, 204, 1)";
      break;
    case 2:
      rgb_color = "rgba(44, 180, 148, 1)";
      break;
    case 3:
      rgb_color = "rgba(136, 64, 156, 1)";
      break;
    case 4:
      rgb_color = "rgba(248, 140, 20, 1)";
      break;
    case 5:
      rgb_color = "rgba(112, 48, 20, 1)";
      break;
    case 6:
      rgb_color = "rgba(204, 224, 208, 1)";
      break;
    case 7:
      rgb_color = "rgba(252, 252, 56, 1)";
      break;
    case 8:
      rgb_color = "rgba(8, 128, 8, 1)";
      break;
    case 9:
      rgb_color = "rgba(252, 252, 124, 1)";
      break;
    case 10:
      rgb_color = "rgba(236, 196, 176, 1)";
      break;
    case 11:
      rgb_color = "rgba(64, 104, 212, 1)";
      break;
  }
  // infoChart.data.datasets[(player-1) * 4].borderColor = rgb_color;
  // infoChart.data.datasets[(player-1) * 4 + 1].borderColor = rgb_color;
  // infoChart.data.datasets[(player-1) * 4 + 1].backgroundColor = rgb_color.replace(/[\d\.]+\)$/g, '0.1)');
  // infoChart.data.datasets[(player-1) * 4 + 2].borderColor = rgb_color;
  // infoChart.data.datasets[(player-1) * 4 + 3].borderColor = rgb_color;

  $(".player_color" + player).css("border-color", rgb_color);
}

function set_nick(player, nick) {
  document.getElementById("nick" + player).innerHTML = nick;
}

function set_supply(player, supply, red) {
  const el = document.getElementById("supply" + player);
  el.innerHTML = supply;
  el.style.color = red ? "red" : "";
}

function set_minerals(player, minerals) {
  document.getElementById("minerals" + player).innerHTML = minerals;
}

function set_gas(player, gas) {
  document.getElementById("gas" + player).innerHTML = gas;
}

function set_workers(player, workers) {
  document.getElementById("workers" + player).innerHTML = workers;
}

function set_army(player, army) {
  document.getElementById("army" + player).innerHTML = army;
}

const race_img_urls = {
  protoss: "https://cdn.glitch.me/550ab268-82e8-4885-95b7-a77978223bf2%2Fprotoss_emblem2.png?v=1636778571156",
  zerg: "https://cdn.glitch.me/550ab268-82e8-4885-95b7-a77978223bf2%2Fzerg_emblem2.png?v=1636778571343",
  terran: "https://cdn.glitch.me/550ab268-82e8-4885-95b7-a77978223bf2%2Fterran_emblem2.png?v=1636778571331"
}
var player_race_cache = [-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1];
function set_race(player, race) {
  if (player_race_cache[player] != race) {
    player_race_cache[player] = race;
    var race_name;
    if (race == 0) {
      race_name = "zerg";
    } else if (race == 1) {
      race_name = "terran";
    } else if (race == 2) {
      race_name = "protoss";
    }
    console.log("setting race emblem for player " + player);
    $("#race" + player).css(
      "background-image",
      `url('${race_img_urls[race_name]}')`
    );
  }
}

function set_apm(player, apm) {
  document.getElementById("apm" + player).innerHTML = apm;
}
