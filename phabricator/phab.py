#!/usr/bin/python3

import argparse, json, os, requests, argparse, re, shutil

from datetime import datetime

from urllib.parse import urlsplit, urlunsplit

def get_issues(url, token, status='open'):
    """
    Get a list of issues from the url using the provided token.

    python requests do not support sending nested dictionaries in URL form
    encoded format, which is the only supported format for the conduit API.

    Apparently, there is no way to encode nested dictionaries in URL form
    encoding format.  To workaround this, PHP invented its own format, which is
    used in conduit API.  The python 'requests' library does not support this
    format

    For this reason the, we have to pass the data in a weird format to be able
    to filter the issues in the server side using the constraints.

    FIXME: The API has a limit of 100 returned issues, this function will only
    get the first 100 if more are available.

    """

    query_url = "{}/maniphest.search".format(url)

    params = {"api.token": token,
              "constraints[statuses][0]": status,
              "attachments[projects][0]": 'true'
             }

    try:
        r = requests.get(query_url, data=params)
        r.raise_for_status()
    except requests.exceptions.HTTPError as err:
        print('Failed to get issues')
        # Uncomment to make the script to stop on an error
        raise SystemExit(err)

    if not r:
        return None

    j = r.json()

    return j['result']['data']

def get_transactions(url, token, task):
    """
    Get the transactions associated with the given task
    """

    query_url = "{}/transaction.search".format(url)

    params = {"api.token": token,
              "objectIdentifier": task,
             }

    try:
        r = requests.get(query_url, data=params)
        r.raise_for_status()
    except requests.exceptions.HTTPError as err:
        print('Failed to get issues')
        # Uncomment to make the script to stop on an error
        raise SystemExit(err)

    if not r:
        return None

    j = r.json()

    return j['result']['data']

def get_user(url, token, uid, known_users):
    """
    Get the user associated with the given ID
    """

    # If the user is cached, return immediately
    if uid in known_users.keys():
        print("returning cached user {}".format(uid))
        return known_users[uid]

    query_url = "{}/user.search".format(url)

    params = {"api.token": token,
              "constraints[phids][0]": uid
             }

    try:
        r = requests.get(query_url, data=params)
        r.raise_for_status()
    except requests.exceptions.HTTPError as err:
        print('Failed to get user')
        # Uncomment to make the script to stop on an error
        raise SystemExit(err)

    if not r:
        return None

    j = r.json()

    # The data returned is a list
    u = j['result']['data'][0]

    known_users[uid] = u

    return u

def get_project(url, token, projectID, known_projects):
    """
    Get the projects details
    """

    # If cached, return immediately
    if projectID in known_projects.keys():
        print("Returning cached project {}".format(projectID))
        return known_projects[projectID]

    query_url = "{}/project.search".format(url)

    params = {"api.token": token,
              "constraints[phids][0]": projectID
             }

    try:
        r = requests.get(query_url, data=params)
        r.raise_for_status()
    except requests.exceptions.HTTPError as err:
        print('Failed to get issues')
        # Uncomment to make the script to stop on an error
        raise SystemExit(err)

    if not r:
        return None

    j = r.json()

    p = j['result']['data']

    known_projects[projectID] = p

    return p

def by_id(x):
    """
    Used to sort comments by id
    """

    return x['id']

def download_file(url, token, file_id):
    """
    Download the file identified by the given ID if not alread downloaded.
    A new directory named after the file ID will be created and the file will be
    downloaded into the directory using the original name.
    """

    filename = 'F{}'.format(file_id)

    if not os.path.isdir(filename):
        try:
            os.mkdir(filename)
        except OSError:
            print('Failed to create directory')

    query_url = "{}/file.search".format(url)

    params = {"api.token": token,
              "constraints[ids][0]": file_id
             }

    try:
        r = requests.get(query_url, data=params)
        r.raise_for_status()
    except requests.exceptions.HTTPError as err:
        print('Failed to get issues')
        # Uncomment to make the script to stop on an error
        raise SystemExit(err)

    if not r:
        return None

    j = r.json()

    if not j['result']['data']:
        print("File not found!")
        return None

    original_name = j['result']['data'][0]['fields']['name']
    uri = j['result']['data'][0]['fields']['dataURI']

    path = os.path.join(filename, original_name)

    # Download file
    try:
        r = requests.get(uri)
        r.raise_for_status()
    except requests.exceptions.HTTPError as err:
        print('Failed to get issues')
        # Uncomment to make the script to stop on an error
        raise SystemExit(err)

    if not r:
        return None

    with open(path, 'wb') as f:
        f.write(r.content)

    return {'file': filename, 'originalName': original_name}

def gitlab_upload_file(url, token, path):
    """
    Upload a file to gitlab

    The given URL must be the project API URL
    """

    query_url = "{}/uploads".format(url)

    try:
        with open(path, 'rb') as f:
            r = requests.post(query_url,
                              headers={"PRIVATE-TOKEN": token},
                              files={'file': f})
        r.raise_for_status()
    except requests.exceptions.HTTPError as err:
        print('Failed to upload file {}'.format(path))
        # Uncomment to make the script to stop on an error
        raise SystemExit(err)

    if not r:
        return None

    response = r.json()

    return response

def replace_files(gitlab_url, gitlab_token, url, token, text):
    """
    Find file references in comments, download the file, upload to gitlab and
    replace the reference
    """

    match = re.search('{F([0-9]*)}', text)

    while match:

        file_id = match.group(1)

        file = download_file(url, token, file_id)

        if not file:
            print('Failed to download file F{}'.format(file_id))
            text = re.sub('{F[0-9]*}', '(File deleted)', text)

            # Get next file
            match = re.search('{F([0-9]*)}', text)

            continue

        path = os.path.join(file['file'], file['originalName'])

        # Upload downloaded file to gitlab
        r = gitlab_upload_file(gitlab_url, gitlab_token, path)

        if not r:
            print('Failed to upload file to gitlab')
            return None

        print(json.dumps(r, indent=4))

        text = re.sub('{F[0-9]*}', r['markdown'], text)

        # Get next file
        match = re.search('{F([0-9]*)}', text)

    return text

def extract_comments(url, token, transactions, known_users):
    """
    Extract comments from transactions.

    Return a list with the new text for the comment with the author and
    timestamp
    """

    comments = []

    comms = list((a for a in transactions if a['type'] =='comment'))

    comms.sort(key=by_id)

    for t in (z for z in comms if z):

        rev = sorted(t['comments'], key=by_id, reverse=True)

        # Assuming the newest version has the largest id
        c = rev[0]

        # Exclude removed comments
        if c['removed']:
            continue

        author_phid = c['authorPHID']
        author = get_user(url, token, author_phid, known_users)
        author_name = author['fields']['username']

        date_created = t['dateCreated']
        date_modified = t['dateModified']

        created = datetime.utcfromtimestamp(date_created).strftime('%Y-%m-%d %H:%M:%S')
        last_mod = datetime.utcfromtimestamp(date_modified).strftime('%Y-%m-%d %H:%M:%S')

        edited = ""
        if created != last_mod:
            edited = " (Edited)"

        content = "{}\n\n----\n\n".format(c['content']['raw'])

        text = "**{} commented on {} UTC{}:**\n\n{}\n".format(author_name,
                                                             last_mod,
                                                             edited,
                                                             content)

        comments.append(text)

    return comments

def gitlab_create_issue(url, token, data):
    """
    Create a gitlab issue from data extracted from phabricator

    :param url:         URL of the repository issues API
    :param token:       User private access token
    :param data:        Gitlab issues API parameters
    """

    query_url = url + '/issues'

    try:
        r = requests.post(query_url,
                          headers={"PRIVATE-TOKEN": token},
                          data=data)
        r.raise_for_status()
    except requests.exceptions.HTTPError as err:
        print('Failed to create issue\n{}'.format(json.dumps(data, indent=4)))
        # Uncomment to make the script to stop on an error
        #raise SystemExit(err)
        return None

    if r:
        response = r.json()
        print("Created: {}".format(response["web_url"]))

    return response["web_url"]

def comment_phab(url, token, issue_phid, gitlab_issue):
    """
    Add a comment to the original phab issue pointing to the new location
    """

    query_url = "{}/maniphest.edit".format(url)

    message = "This issue was moved to gitlab: {}".format(gitlab_issue)

    params = {"api.token": token,
              "transactions[0][type]": 'comment',
              "transactions[0][value]": message,
              "objectIdentifier": issue_phid
             }

    try:
        r = requests.get(query_url, data=params)
        r.raise_for_status()
    except requests.exceptions.HTTPError as err:
        print('Failed to get issues')
        # Uncomment to make the script to stop on an error
        raise SystemExit(err)

    if not r:
        return None

    j = r.json()

    print(json.dumps(j, indent=4))

    return j['error_code']

def migrate_issue(url, token, gitlab_url, gitlab_token, issue):
    """
    Migrate the issue and add a comment to original issue with the new location
    """

    issue_id = issue['name']
    title = issue['issue']['fields']['name']
    description = issue['issue']['fields']['description']['raw']
    comments = issue['comments']
    confidential = issue['confidential']
    author = issue['author']['fields']['username']

    # Add original name to title
    new_title = "{}: {}".format(issue_id, title)

    # Construct original url for the issue
    spliturl = urlsplit(url)
    original = urlunsplit((spliturl.scheme, spliturl.netloc, issue_id, None,
                           None))

    # Download eventual files, upload to Gitlab and replace references
    if (comments):
        c_temp = replace_files(gitlab_url, gitlab_token, url, token,
                               comments)
        new_comments = "\n### Comments:\n\n{}".format(c_temp)

    d_temp = replace_files(gitlab_url, gitlab_token, url, token, description)

    template = "### Description\n\n**Originally reported by {}: {}**\n\n{}\n"

    new_description = template.format(author,
                                      original,
                                      d_temp)

    if (comments):
        new_description = new_description + new_comments

    confidential = issue['confidential']

    data = {'title': new_title,
            'description': new_description,
            'confidential': confidential}

    uri = gitlab_create_issue(gitlab_url, gitlab_token, data)

    if uri:
        comment_phab(url, token, issue['issue']['phid'], uri)

    return data

def print_to_files(data):
    """
    Print extracted data to files
    """

    for d in data:
        phid = d['name']
        with open('{}-trans.json'.format(phid), 'w') as f:
            print(json.dumps(d['transactions'], indent=4), file=f)

        with open('{}-issue.json'.format(phid), 'w') as f:
            print(json.dumps(d['issue'], indent=4), file=f)

        with open('{}-author.json'.format(phid), 'w') as f:
            print(json.dumps(d['author'], indent=4), file=f)

        with open('{}-comments.json'.format(phid), 'w') as f:
            print(d['comments'], file=f)

        with open('{}-project.json'.format(phid), 'w') as f:
            print(d['project'], file=f)

def run(url, token, gitlab_url, gitlab_token):

    issues = get_issues(url, token)

    known_users = {}
    known_projects = {}

    complete = []
    for i in issues:
        t = get_transactions(url, token, i['phid'])
        u = get_user(url, token, i['fields']['authorPHID'], known_users)
        c = extract_comments(url, token, t, known_users)

        if (i['attachments']['projects']['projectPHIDs']):
            p = get_project(url, token,
                            i['attachments']['projects']['projectPHIDs'][0],
                            known_projects)
        else:
            p = None

        confidential = False
        if (i['fields']['policy']['view'] != 'public'):
            confidential = True

        comments = "".join(c)

        issue_data = {'name': 'T{}'.format(i['id']),
                      'issue': i,
                      'transactions': t,
                      'author': u,
                      'comments': comments,
                      'confidential': confidential,
                      'project': p
                     }

        complete.append(issue_data)

        data = migrate_issue(url, token, gitlab_url, gitlab_token, issue_data)

        with open('{}-data.txt'.format(issue_data['name']), 'w') as f:
            print(data['title'], file=f)
            print(data['description'], file=f)

    print_to_files(complete)

    return complete

def main():

    parser = argparse.ArgumentParser()

    parser.add_argument("--url", help="Phabricator API URL", required=True)
    parser.add_argument("--token", help="Phabricator authentication token",
                        required = True)
    parser.add_argument("--gitlab-url", help="Gitlab API URL", dest='gitlab',
                        required = True)
    parser.add_argument("--gitlab-token", help="Gitlab authentication token",
                        required = True, dest = 'g_token')

    args = parser.parse_args()

    run(args.url, args.token, args.gitlab, args.g_token)

if __name__ == "__main__":
    main()

