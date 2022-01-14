// utility to copy.bat for replacing something in titan.js

const fs = require("fs");

const titanJS = fs.readFileSync("./web/titan.js", "utf-8");
fs.writeFileSync("D:\\dev\\titan-reactor\\packages\\titan-reactor\\src\\renderer\\openbw\\titan.js", titanJS.replace("import.meta.url", `""`));