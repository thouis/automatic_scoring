import urllib2
import json
import time
import os


def game_pages():
    url = 'http://online-go.com/api/v1/games/?outcome__lt="9"'  # doens't work, but filters resignations
    while url:
        time.sleep(0.25)
        print url
        page = json.load(urllib2.urlopen(url))
        yield page
        url = page["next"]

def score_difference(game):
    outcome = game["outcome"]
    try:
        return float(outcome.split(" ")[0])
    except:
        # handles all non-numeric outcomes: Timeout, Resign, etc.
        return 100


def get_page_with_wait(url, wait=6):  # SGF throttling is 10/minute
    if wait <= 0:
        wait = 0.01
    try:
        time.sleep(wait)
        response = urllib2.urlopen(url)
    except urllib2.HTTPError as e:
        if e.code == 429:  # too many requests
            print("Too many requests / minute, falling back to {} seconds between fetches.".format(int(1.5 * wait)))
            # exponential falloff
            return get_page_with_wait(url, wait=(1.5 * wait))
        raise
    else:
        # everything is fine
        return response.read()

def save_sgf(out_filename, SGF_URL):
    if os.path.exists(out_filename):
        print("Skipping {} because it has already been downloaded.".format(out_filename))
    else:
        print("Downloading {}...".format(out_filename))
        sgf = get_page_with_wait(SGF_URL)
        if "undefined" in sgf:
            print "\tcould not download", SGF_URL
            return
        with open(out_filename, "w") as f:
            f.write(sgf)

if __name__ == "__main__":
    for gp in game_pages():
        for game in gp["results"]:
            # ignore rectangular games
            if game["width"] != game["height"]:
                continue

            # ignore games with too large a point difference at the end
            if score_difference(game) > 15:
                continue

            players = game["players"]

            # ignore missing data
            if ("rating" not in players["white"]) or ("rating" not in players["black"]):
                continue

            # only take games where the average rating of the players is > 1d
            if (players["white"]["rating"] + players["black"]["rating"]) / 2 < 2100:
                continue

            print game["outcome"],

            game_id = game["id"]
            # fetch SGF
            save_sgf("OGS_game_{}.sgf".format(game_id),
                     "http://online-go.com/api/v1/games/{}/sgf".format(game_id))
