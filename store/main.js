//global vars
var categories = [];
var install_button;
var uninstall_button;
var devstring;
var pkg_links = [];
var download_button;
var control_button;
var pbc;
var pb;
var pbt;
var reopen_game_url;
var reopen_game_id;
var games_json;
var pkglink;
var progress_update_interval;

function set_category() {
    for (i = 0; i < categories.length; i++) {
        document.getElementById("filter_" + i).checked = false;
    }
    document.getElementById("filter").hidden = true;
    document.getElementById("filter_" + categories.indexOf(category)).checked = true;

    if (pkglink == "") {
        search("pkgs.json");
    } else {
        search(pkglink)
    }
}

function search(url) {

    //read checkboxes
    var filters = [];
    for (i = 0; i < categories.length; i++) {
        if (document.getElementById("filter_" + i).checked == true) {
            filters.push(categories[i]);
        }
    }
    //remove everything
    for (i = 0; i < games_json.length; i++) {
        var element = document.getElementById("game_" + i);
        if (element != null) {
            element.parentNode.removeChild(element);
        }
    }

    for (i = 0; i < games_json.length; i++) {
        if (games_json[i].categories.some(r => filters.includes(r)) && (games_json[i].name.toUpperCase().includes(document.getElementById("searchbar").value.toUpperCase()) || games_json[i].titleid.toUpperCase().includes(document.getElementById("searchbar").value))) {
            var x = document.createElement("DIV");
            x.setAttribute("id", "game_" + i);
            x.setAttribute("class", "game");
            x.setAttribute("onclick", "opengame(\"" + url + "\", " + i + ")");
            var y = document.createElement("IMG");
            y.setAttribute("class", "icon");
            y.setAttribute("src", games_json[i].icon);
            var z = document.createTextNode(games_json[i].name);
            x.appendChild(y);
            x.appendChild(document.createElement("br"));
            x.appendChild(z);
            document.getElementById("center").appendChild(x);
        }
    }
}

function interval(func, wait, times) {
    var interv = function(w, t) {
        return function() {
            if (typeof t === "undefined" || t-- > 0) {
                setTimeout(interv, w);
                try {
                    func.call(null);
                } catch (e) {
                    t = 0;
                    throw e.toString();
                }
            }
        };
    }(wait, times);

    setTimeout(interv, wait);
};

function setCookie(cname, cvalue, exdays) {
    var d = new Date();
    d.setTime(d.getTime() + (exdays * 24 * 60 * 60 * 1000));
    var expires = "expires=" + d.toGMTString();
    document.cookie = cname + "=" + cvalue + ";" + expires + ";path=/";
}

function getCookie(cname) {
    var name = cname + "=";
    var decodedCookie = decodeURIComponent(document.cookie);
    var ca = decodedCookie.split(';');
    for (var i = 0; i < ca.length; i++) {
        var c = ca[i];
        while (c.charAt(0) == ' ') {
            c = c.substring(1);
        }
        if (c.indexOf(name) == 0) {
            return c.substring(name.length, c.length);
        }
    }
    return "";
}

function compare(a, b) {
    if (a.name.toUpperCase() < b.name.toUpperCase())
        return -1;
    if (a.name.toUpperCase() > b.name.toUpperCase())
        return 1;
    return 0;
}

function request(url) {
    reopen_game_url = url;
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
            games_json = JSON.parse(xhttp.responseText);
            games_json.sort(compare);
            //populate pkgs
            for (i = 0; i < games_json.length; i++) {
                //find all categories
                for (j = 0; j < games_json[i].categories.length; j++) {
                    if (!categories.includes(games_json[i].categories[j])) {
                        categories.push(games_json[i].categories[j])
                    }
                }

                var x = document.createElement("DIV");
                x.setAttribute("id", "game_" + i);
                x.setAttribute("class", "game");
                x.setAttribute("onclick", "opengame(\"" + url + "\", " + i + ")");
                var y = document.createElement("IMG");
                y.setAttribute("class", "icon");
                y.setAttribute("src", games_json[i].icon);
                var z = document.createTextNode(games_json[i].name);
                x.appendChild(y);
                x.appendChild(document.createElement("br"));
                x.appendChild(z);
                document.getElementById("center").appendChild(x);
            }

            //populate filters
            for (i = 0; i < categories.length; i++) {
                var x = document.createElement("DIV");
                x.setAttribute("class", "filter")
                var y = document.createElement("INPUT");
                y.setAttribute("checked", "true");
                y.setAttribute("class", "setcursor");
                if (pkglink == "") {
                    y.setAttribute("onclick", "search(\"pkgs.json\")");
                } else {
                    y.setAttribute("onclick", "search(\"" + pkglink + "\")");
                }
                y.setAttribute("id", "filter_" + i);
                y.setAttribute("type", "checkbox");
                var z = document.createElement("LABEL");
                z.setAttribute("class", "setcursor");
                z.innerText = categories[i] + ": ";
                z.setAttribute("for", y.id)
                x.appendChild(z);
                x.appendChild(y);
                document.getElementById("filter").appendChild(x);
            }

            if (typeof category !== 'undefined') {
                set_category();
            }
        }
    };
    xhttp.open("GET", url, true);
    xhttp.send();
}

function find_task(contentid, type, titleid, gameid) {
    var xhttp = new XMLHttpRequest();
	xhttp.onerror = function () {
    alert("Couldn't connect to PS4.\nMake sure the IP is correct and that remote package installer is running");
	};
    xhttp.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
            var response = xhttp.responseText.replace("0x", "");
            response = JSON.parse(response);

            if (response.status == "success") {
                var http = new XMLHttpRequest();
                http.open('POST', "http://" + getCookie("PS4IP") + ":12800/api/get_task_progress", true);
                http.onreadystatechange = function() {
                    if (http.readyState == 4 && http.status == 200) {
                        var res = http.responseText.replace(/": /g, "\": \"");
                        res = res.replace(/, "/g, "\", \"");
                        res = res.replace(/""/g, "\"");
                        res = res.replace(" }", "\" }");
                        res = JSON.parse(res);
                        if (res.transferred_total == res.length_total) {
                            status(titleid, type);
                        } else {
                            spawn_progressbar(response.task_id, titleid, contentid, type);
                        }
                    }
                }
                http.send("{\"task_id\":" + response.task_id + "}")
            } else {
                status(titleid, type);
            }
        }
    };
    xhttp.open("POST", "http://" + getCookie("PS4IP") + ":12800/api/find_task", true);

    switch (type) {
        case "game":
            xhttp.send("{\"content_id\":\"" + contentid + "\",\"sub_type\":6}");
            break;
        case "additional content":
            xhttp.send("{\"content_id\":\"" + contentid + "\",\"sub_type\":7}");
            break;
        case "patch":
            xhttp.send("{\"content_id\":\"" + contentid + "\",\"sub_type\":8}");
            break;
        case "theme":
            xhttp.send("{\"content_id\":\"" + contentid + "\",\"sub_type\":9}");
    }
}

function opengame(url, gameid) {


    reopen_game_id = gameid;
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
            var response = JSON.parse(xhttp.responseText);
            response.sort(compare);

            var linkstring = "\"";
            for (i = 0; i < response[gameid].links.length - 1; i++) {
                linkstring = linkstring + response[gameid].links[i] + "\", \"";
            }
            linkstring = linkstring + response[gameid].links[response[gameid].links.length - 1] + "\"";
			pkg_links = response[gameid].links;

            var element = document.getElementById("searchbar");
            if (element != null) {
                document.getElementById("searchbar").parentNode.removeChild(document.getElementById("searchbar"));
            }

            for (i = 0; i < response.length; i++) {
                var element = document.getElementById("game_" + i);
                if (element != null) {
                    element.parentNode.removeChild(element);
                }
            }
            element = document.getElementById("headline");
            if (element != null) {
                element.parentNode.removeChild(element);
            }

            element = document.getElementById("filter");
            if (element != null) {
                element.parentNode.removeChild(element);
            }


            var headline = document.createElement("font");
            headline.setAttribute("size", "12");

            pbc = document.createElement("DIV");
            pbc.setAttribute("id", "pbc");
            pb = document.createElement("DIV");
            pb.setAttribute("id", "pb");
            pbt = document.createElement("DIV");
            pbt.setAttribute("id", "pbt");


            install_button = document.createElement("button");
            install_button.setAttribute("class", "button");
            install_button.setAttribute("onclick", "install(" + linkstring + ", \"" + response[gameid].titleid + "\", \"" + response[gameid].contentid + "\", \"" + response[gameid].type + "\")");

            uninstall_button = document.createElement("button");
            uninstall_button.setAttribute("class", "button");
            uninstall_button.setAttribute("onclick", "uninstall(\"" + response[gameid].titleid + "\", \"" + response[gameid].contentid + "\", \"" + response[gameid].type + "\")");

			devstring = "Devs: ";
			for(var i = 0; i < response[gameid].devs.length; i++)
			{
				devstring += response[gameid].devs[i] + ', ';
			}
			devstring = devstring.slice(0, -2);
			
			pkg_size = response[gameid].size;

            var w = document.createElement("DIV");
            w.setAttribute("class", "centerer");
            w.setAttribute("id", "open_game_container")

            var x = document.createElement("DIV");
            x.setAttribute("id", "game_" + gameid);
            x.setAttribute("class", "game_open");
            x.setAttribute("align", "center");

            var y = document.createElement("IMG");
            y.setAttribute("src", response[gameid].icon);
            y.setAttribute("class", "icon_open");

            var z = document.createElement("DIV");
            z.setAttribute("id", "status");

            var buttondiv = document.createElement("DIV");
            buttondiv.setAttribute("id", "buttondiv");

            headline.appendChild(document.createTextNode(response[gameid].name));
            pbc.appendChild(pb);
            pbc.appendChild(pbt);
            x.appendChild(y);
            x.appendChild(document.createElement("br"));
            x.appendChild(z);
            x.appendChild(buttondiv);
            w.appendChild(headline);
            w.appendChild(document.createElement("br"));
            w.appendChild(x);
            document.body.appendChild(w);
            find_task(response[gameid].contentid, response[gameid].type, response[gameid].titleid, gameid);
        }
    };
    xhttp.open("GET", url, true);
    xhttp.send();
}

function status(titleid, type) {
    var http = new XMLHttpRequest();
    http.open('POST', "http://" + getCookie("PS4IP") + ":12800/api/is_exists", true);
    http.onreadystatechange = function() {
        if (http.readyState == 4 && http.status == 200) {
            if (http.responseText.includes("false")) {
                var response = JSON.parse(http.responseText);
            } else {
                var response = http.responseText.replace("\"size\": ", "\"size\": \"");
                response = response.replace(" }", "\" }");
                response = JSON.parse(response);
            }
            if (response.exists != "true") {
                switch (type) {
                    case "game":
                        document.getElementById("status").appendChild(document.createTextNode("status: not installed"));
                        spawn_installbutton();
                        break;

                    case "patch":
                        document.getElementById("status").appendChild(document.createTextNode("status:  main game not installed"));
                        break;

                    case "theme":
                        document.getElementById("status").appendChild(document.createTextNode("status:  unknown (theme)"));
                        spawn_installbutton();
                        spawn_uninstallbutton();
                        break;

                    case "ac":
                        document.getElementById("status").appendChild(document.createTextNode("status:  main game not installed"));
                }

            } else {
                switch (type) {
                    case "game":
                        document.getElementById("status").appendChild(document.createTextNode("status: installed"));
                        document.getElementById("status").appendChild(document.createElement("br"));
                        var size = Math.round(parseInt(response.size) / 1000000);
                        document.getElementById("status").appendChild(document.createTextNode("size: " + size + " mb"));
                        spawn_uninstallbutton();
                        break;

                    case "patch":
                        document.getElementById("status").appendChild(document.createTextNode("status: main game installed"));
                        spawn_installbutton();
                        spawn_uninstallbutton();
                        break;

                    case "theme":
                        document.getElementById("status").appendChild(document.createTextNode("status:  unknown (theme)"));
                        spawn_installbutton();
                        spawn_uninstallbutton();
                        break;

                    case "ac":
                        document.getElementById("status").appendChild(document.createTextNode("status: main game installed"));
                        spawn_installbutton();
                        spawn_uninstallbutton();
                }

            }
            spawn_info();
        }
    }
    http.send("{\"title_id\":\"" + titleid + "\"}");
}


function install(linkstring, titleid, contentid, type) {
    var http = new XMLHttpRequest();
    http.open('POST', "http://" + getCookie("PS4IP") + ":12800/api/install", true);
    http.onreadystatechange = function() {
        if (http.readyState == 4 && http.status == 200) {
            var response = JSON.parse(http.responseText);
            if (response.status == "success") {
                document.getElementById("status").innerHTML = "status: currently installing";
                console.log(http.responseText + "installing");
                spawn_progressbar(response.task_id, titleid, contentid, type);
            } else {
                alert("cant install pkg");
            }
        }
    }
    http.send("{\"type\":\"direct\",\"packages\":[\"" + linkstring + "\"]}");
}

function uninstall(titleid, contentid, type) {
    var http = new XMLHttpRequest();
    http.open('POST', "http://" + getCookie("PS4IP") + ":12800/api/uninstall_" + type, true);
    http.onreadystatechange = function() {
        if (http.readyState == 4 && http.status == 200) {
            var response = JSON.parse(http.responseText);
            if (response.status == "success") {
                document.getElementById("open_game_container").parentNode.removeChild(document.getElementById("open_game_container"));
                opengame(reopen_game_url, reopen_game_id);
            } else {
                alert("cant uninstall pkg");
            }
        }
    }
    if (type == "game" || type == "patch") {
        http.send("{\"title_id\":\"" + titleid + "\"}");
    } else {
        http.send("{\"content_id\":\"" + contentid + "\"}");
    }
}

function spawn_installbutton() {
    install_button.appendChild(document.createTextNode("install"));
    document.getElementById("buttondiv").appendChild(install_button);
    document.getElementById("buttondiv").appendChild(document.createElement("br"));
}

function spawn_uninstallbutton() {
    uninstall_button.appendChild(document.createTextNode("uninstall"));
    document.getElementById("buttondiv").appendChild(uninstall_button);
    document.getElementById("buttondiv").appendChild(document.createElement("br"));
}

function spawn_info() {
	var description = document.createElement("DIV");
    description.setAttribute("id", "description");
	description.appendChild(document.createTextNode("Description:"));
	for(var i = 0; i < games_json[reopen_game_id].description.length; i++)
	{
		description.appendChild(document.createElement("BR"));
		description.appendChild(document.createTextNode(games_json[reopen_game_id].description[i]));
	}
	document.getElementById("buttondiv").appendChild(description);
	
	var developers = document.createElement("DIV");
    developers.setAttribute("id", "devs");		
	developers.appendChild(document.createTextNode(devstring));
	document.getElementById("buttondiv").appendChild(developers);
	
	var size = document.createElement("DIV");
    size.setAttribute("id", "devs");		
	size.appendChild(document.createTextNode("Size: " + pkg_size));
	document.getElementById("buttondiv").appendChild(size);
	
	if(!navigator.userAgent.includes("PlayStation 4"))
	{
		for(var i = 0; i < pkg_links.length;i++){
		var download_button = document.createElement("A");
		download_button.setAttribute("class", "button");
		download_button.setAttribute("id", "download"+i);
		download_button.setAttribute("href", pkg_links[i]);
		download_button.appendChild(document.createTextNode("Download Part " + (i+1)));
		document.getElementById("buttondiv").appendChild(download_button);
		document.getElementById("buttondiv").appendChild(document.createElement("BR"));
		}
	}
	
	var related_pkgs = document.createElement("DIV");
    related_pkgs.setAttribute("id", "related_pkgs");
	related_pkgs.appendChild(document.createTextNode("Related PKGs:"));
	related_pkgs.appendChild(document.createElement("BR"));
	for (i = 0; i < games_json.length; i++) {
        if (games_json[i].titleid == games_json[reopen_game_id].titleid && i != reopen_game_id) {
            var x = document.createElement("BUTTON");
            x.setAttribute("class", "button");
			x.appendChild(document.createTextNode(games_json[i].name));
            x.setAttribute("onclick", "opengame(\"" + reopen_game_url + "\", " + i + ")");
			related_pkgs.appendChild(x);
			related_pkgs.appendChild(document.createElement("BR"));
        }
	}
	document.getElementById("buttondiv").appendChild(related_pkgs);
	
    var back_button = document.createElement("A");
    back_button.setAttribute("class", "button");
    back_button.setAttribute("href", "index.html");
    back_button.appendChild(document.createTextNode("back"));
    document.getElementById("buttondiv").appendChild(back_button);
}

function pause(task_id) {
    get_progress(task_id);
    if (pb.style.width != "100%") {
        var http = new XMLHttpRequest();
        http.open('POST', "http://" + getCookie("PS4IP") + ":12800/api/pause_task", true);
        http.onreadystatechange = function() {
            if (http.readyState == 4 && http.status == 200) {
                var res = JSON.parse(http.responseText);
                if (res.status == "success") {
                    control_button.setAttribute("onclick", "resume(" + task_id + ")");
                    control_button.innerHTML = "&#9658;";
                } else {
                    alert("cant pause task");
                }
            }

        }
        http.send("{\"task_id\":" + task_id + "}");
    }
}

function resume(task_id) {
    var http = new XMLHttpRequest();
    http.open('POST', "http://" + getCookie("PS4IP") + ":12800/api/resume_task", true);
    http.onreadystatechange = function() {
        if (http.readyState == 4 && http.status == 200) {
            var res = JSON.parse(http.responseText);
            if (res.status == "success") {
                control_button.setAttribute("onclick", "pause(" + task_id + ")");
                control_button.innerHTML = "&#9616;&nbsp;&#9612;";
            } else {
                alert("cant resume task");
            }
        }

    }
    http.send("{\"task_id\":" + task_id + "}");
}

function cancel(task_id, titleid, contentid, type) {
    var http = new XMLHttpRequest();
    http.open('POST', "http://" + getCookie("PS4IP") + ":12800/api/unregister_task", true);
    http.onreadystatechange = function() {
        if (http.readyState == 4 && http.status == 200) {
            var res = JSON.parse(http.responseText);
            if (res.status == "success") {
                uninstall(titleid, contentid, type);
            } else {
                alert("cant cancel task");
            }
        }

    }
    http.send("{\"task_id\":" + task_id + "}");
}

function spawn_progressbar(task_id, titleid, contentid, type) {
    document.getElementById("buttondiv").innerHTML = "";
    pb.style.width = "0%";
    pbt.appendChild(document.createTextNode("0%"));
    document.getElementById("buttondiv").appendChild(document.createElement("br"));
    document.getElementById("buttondiv").appendChild(pbc);
    document.getElementById("buttondiv").appendChild(document.createElement("br"));
    document.getElementById("buttondiv").appendChild(document.createElement("br"));

    control_button = document.createElement("button");
    control_button.setAttribute("class", "button");
    control_button.setAttribute("onclick", "pause(" + task_id + ")");
    control_button.innerHTML = "&#9616;&nbsp;&#9612;";
    document.getElementById("buttondiv").appendChild(control_button);
    document.getElementById("buttondiv").appendChild(document.createElement("br"));

    cancel_button = document.createElement("button");
    cancel_button.setAttribute("class", "button");
    cancel_button.setAttribute("onclick", "cancel(\"" + task_id + "\", \"" + titleid + "\", \"" + contentid + "\", \"" + type + "\")");
    cancel_button.innerHTML = "cancel";
    document.getElementById("buttondiv").appendChild(cancel_button);
    document.getElementById("buttondiv").appendChild(document.createElement("br"));
    progress_update_interval = setInterval(get_progress, 300, task_id);
    spawn_info();
}

function get_progress(task_id) {
    var http = new XMLHttpRequest();
    http.open('POST', "http://" + getCookie("PS4IP") + ":12800/api/get_task_progress", true);
    http.onreadystatechange = function() {
        if (http.readyState == 4 && http.status == 200) {
            var res = http.responseText.replace(/": /g, "\": \"");
            res = res.replace(/, "/g, "\", \"");
            res = res.replace(/""/g, "\"");
            res = res.replace(" }", "\" }");
            res = JSON.parse(res);
            res = Math.round(parseInt(res.transferred_total) / (parseInt(res.length_total) / 100));
            pb.style.width = res + "%";
            pbt.innerHTML = res + "%";
            if (res == 100) {
                document.getElementById("open_game_container").parentNode.removeChild(document.getElementById("open_game_container"));
                clearInterval(progress_update_interval);
                opengame(reopen_game_url, reopen_game_id);
            }
        }

    }
    http.send("{\"task_id\":" + task_id + "}");
}
