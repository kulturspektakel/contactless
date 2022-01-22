from os import environ

if environ.get("GITHUB_RUN_NUMBER") is None:
    print("Enter build number:")
    os.environ["GITHUB_RUN_NUMBER"] = input()
