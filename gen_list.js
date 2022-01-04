const fs = require('fs');

// directory path
const dir = './all_files_esm';

// list all files in the directory
fs.readdir(dir, (err, files) => {
    if (err) {
        throw err;
    }

    // files object contains all files names
    // log them on console
    files.forEach(file => {
        console.log(file);
    });
    fs.writeFileSync('./web/list.js', 
        ["export default", 
        JSON.stringify(files)].join(" ")
        
    );
});

