import requests
import sys
import argparse
import os
from datetime import datetime, timedelta

def fetch_and_print_prs(repo_owner, repo_name, states, token=None, output_file=None, since_date_str=None):
    """
    Fetches pull requests for a given GitHub repository based on state and date,
    and prints them as a Markdown list, grouped by state.

    Args:
        repo_owner (str): The owner of the GitHub repository.
        repo_name (str): The name of the GitHub repository.
        states (list): A list of states to fetch ('open', 'closed', 'merged').
        token (str, optional): A GitHub personal access token for authentication.
        output_file (str, optional): Path for the output Markdown file.
        since_date_str (str, optional): ISO formatted date string (YYYY-MM-DD).
    """
    headers = {
        "Accept": "application/vnd.github.v3+json",
        "X-GitHub-Api-Version": "2022-11-28"
    }
    if token:
        headers["Authorization"] = f"token {token}"

    all_results = {}
    search_api_url = "https://api.github.com/search/issues"

    # Use a set to avoid duplicate requests
    for state in set(states):
        print(f"Fetching '{state}' pull requests from {repo_owner}/{repo_name}...", file=sys.stderr)
        
        # Build the search query for the GitHub API
        query_parts = [f"repo:{repo_owner}/{repo_name}", "is:pr"]
        if state == 'open':
            query_parts.append("is:open")
        elif state == 'merged':
            query_parts.append("is:merged")
        elif state == 'closed':
            # Specifically fetch PRs that were closed but not merged
            query_parts.append("is:unmerged is:closed")
        
        if since_date_str:
            # Apply date filter based on the PR state
            if state in ['closed', 'merged']:
                # For closed and merged PRs, filter by the date they were closed/merged
                query_parts.append(f"closed:>{since_date_str}")
            else: # state == 'open'
                # For open PRs, filter by the date they were created
                query_parts.append(f"created:>{since_date_str}")
        
        query = " ".join(query_parts)
        # For merged and closed PRs, it's more useful to sort by when they were updated
        sort_field = "updated" if state in ['merged', 'closed'] else "created"
        params = {"q": query, "per_page": 100, "sort": sort_field, "order": "desc"}

        try:
            response = requests.get(search_api_url, headers=headers, params=params, timeout=15)
            response.raise_for_status()
            # The search results are in the 'items' key
            all_results[state] = response.json().get('items', [])
        except requests.exceptions.RequestException as e:
            print(f"An error occurred while fetching '{state}' PRs: {e}", file=sys.stderr)
            all_results[state] = []

    # Prepare the output lines, grouped by state
    output_lines = []
    for state, pr_list in all_results.items():
        # Clarify the 'closed' state in the header
        header_state = 'Closed (Unmerged)' if state == 'closed' else state.capitalize()
        output_lines.append(f"\n--- {header_state} Pull Requests ---\n\n")
        
        if not pr_list:
            output_lines.append("No pull requests found for this state and date range.\n")
            continue

        for pr in pr_list:
            pr_number = pr.get('number')
            pr_title = pr.get('title')
            pr_url = pr.get('html_url')

            if pr_number is not None and pr_title and pr_url:
                output_lines.append(f"* [**#{pr_number}** - {pr_title}]({pr_url})\n")

    # Write to output file or print to stdout
    if output_file:
        with open(output_file, 'w', encoding='utf-8') as f:
            f.writelines(output_lines)
        print(f"Successfully wrote PRs to {output_file}", file=sys.stderr)
    else:
        for line in output_lines:
            print(line, end='')

def get_token_from_file():
    """Reads a GitHub token from ~/.config/github_token."""
    token_path = os.path.expanduser("~/.config/github_token")
    if os.path.exists(token_path):
        try:
            with open(token_path, 'r', encoding='utf-8') as f:
                return f.read().strip()
        except IOError as e:
            print(f"Warning: Could not read token file at {token_path}: {e}", file=sys.stderr)
    return None

def validate_date(date_string):
    """Validate that the date string is in YYYY-MM-DD format."""
    try:
        datetime.strptime(date_string, '%Y-%m-%d')
        return date_string
    except ValueError:
        raise argparse.ArgumentTypeError(f"'{date_string}' is not a valid date. Please use YYYY-MM-DD format.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Fetch pull requests from a GitHub repository by state and date."
    )
    parser.add_argument(
        "repository",
        help="The GitHub repository to query, in 'owner/repo' format."
    )
    parser.add_argument(
        "-s", "--state",
        action="append",
        choices=['open', 'closed', 'merged'],
        help="State of the pull requests. 'closed' fetches unmerged PRs. "
             "Can be specified multiple times. Defaults to 'open'."
    )
    
    # Group for mutually exclusive date filters
    date_group = parser.add_mutually_exclusive_group()
    date_group.add_argument(
        "--since",
        type=validate_date,
        help="Fetch PRs. For open, filters by creation date. For closed/merged, filters by closed/merged date. (YYYY-MM-DD)"
    )
    date_group.add_argument(
        "--days-back",
        type=int,
        help="Fetch PRs from the last N days. For open, uses creation date. For closed/merged, uses closed/merged date."
    )

    parser.add_argument(
        "-t", "--token",
        help="GitHub personal access token. If not provided, tries to read from ~/.config/github_token."
    )
    parser.add_argument(
        "-o", "--output",
        help="Output file for the Markdown list. Prints to standard output if not specified."
    )

    args = parser.parse_args()

    try:
        owner, repo = args.repository.split('/')
    except ValueError:
        print("Error: Repository must be in 'owner/repo' format.", file=sys.stderr)
        sys.exit(1)

    pull_request_states = args.state if args.state else ['open']
    
    since_date = args.since
    if args.days_back is not None:
        start_date = datetime.now() - timedelta(days=args.days_back)
        since_date = start_date.strftime('%Y-%m-%d')
    
    auth_token = args.token or get_token_from_file()
    if auth_token and not args.token:
        print("Using token found in ~/.config/github_token.", file=sys.stderr)

    fetch_and_print_prs(owner, repo, states=pull_request_states, token=auth_token, output_file=args.output, since_date_str=since_date)

