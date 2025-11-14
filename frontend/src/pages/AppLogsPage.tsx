import { useCallback, useEffect, useState } from 'react';
import { apiGet } from '../api/client';
import type { AppLogEntry } from '../types';

export function AppLogsPage() {
  const [logs, setLogs] = useState<AppLogEntry[]>([]);
  const [levelFilter, setLevelFilter] = useState<'ALL' | 'INFO' | 'WARN' | 'ERROR'>('ALL');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const fetchLogs = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const params = new URLSearchParams({ limit: '100' });
      if (levelFilter !== 'ALL') {
        params.set('level', levelFilter);
      }
      const data = await apiGet<AppLogEntry[]>(`/api/logs/app?${params.toString()}`);
      setLogs(data);
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setLoading(false);
    }
  }, [levelFilter]);

  useEffect(() => {
    void fetchLogs();
  }, [fetchLogs]);

  return (
    <section className="card">
      <div className="card-header">
        <h2>Application Logs</h2>
        <div className="actions">
          <button type="button" onClick={() => fetchLogs()} disabled={loading}>
            Refresh
          </button>
        </div>
      </div>
      <div className="filters">
        <label>
          Level
          <select
            value={levelFilter}
            onChange={(event) => setLevelFilter(event.target.value as 'ALL' | 'INFO' | 'WARN' | 'ERROR')}
          >
            <option value="ALL">All</option>
            <option value="INFO">Info</option>
            <option value="WARN">Warning</option>
            <option value="ERROR">Error</option>
          </select>
        </label>
      </div>
      {error && <div className="error">{error}</div>}
      <div className="table-wrapper">
        <table>
          <thead>
            <tr>
              <th>Level</th>
              <th>Message</th>
              <th>Timestamp</th>
            </tr>
          </thead>
          <tbody>
            {logs.map((log) => (
              <tr key={log.id}>
                <td>{log.level}</td>
                <td>{log.message}</td>
                <td>{log.timestamp_utc ? new Date(log.timestamp_utc).toLocaleString() : '—'}</td>
              </tr>
            ))}
            {logs.length === 0 && !loading && (
              <tr>
                <td colSpan={3} style={{ textAlign: 'center' }}>
                  No application logs recorded.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </section>
  );
}
