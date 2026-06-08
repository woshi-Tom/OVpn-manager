from db import execute_query

def log_event(level, source, message):
    try:
        execute_query(
            "INSERT INTO system_logs (level, source, message) VALUES (%s, %s, %s)",
            (level, source, message)
        )
    except Exception as e:
        print(f"Failed to write system log: {e}")
