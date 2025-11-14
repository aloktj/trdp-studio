import { useCallback, useEffect, useState } from 'react';
import { apiGet } from '../api/client';
import type { TrdpLogEntry } from '../types';

function previewPayload(hex: string) {
  if (!hex) {
    return '—';
  }
  if (hex.length <= 32) {
    return hex;
  }
  return `${hex.slice(0, 32)}…`;
}

export function TrdpLogsPage() {
  const [logs, setLogs] = useState<TrdpLogEntry[]>([]);
  const [typeFilter, setTypeFilter] = useState<'ALL' | 'PD' | 'MD'>('ALL');
  const [directionFilter, setDirectionFilter] = useState<'ALL' | 'IN' | 'OUT'>('ALL');
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const fetchLogs = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const params = new URLSearchParams({ limit: '100' });
      if (typeFilter !== 'ALL') {
        params.set('type', typeFilter);
      }
      if (directionFilter !== 'ALL') {
        params.set('direction', directionFilter);
      }
      const data = await apiGet<TrdpLogEntry[]>(`/api/logs/trdp?${params.toString()}`);
      setLogs(data);
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setLoading(false);
    }
  }, [directionFilter, typeFilter]);

  useEffect(() => {
    void fetchLogs();
  }, [fetchLogs]);

  return (
    <section className="card">
      <div className="card-header">
        <h2>TRDP Logs</h2>
        <div className="actions">
          <button type="button" onClick={() => fetchLogs()} disabled={loading}>
            Refresh
          </button>
        </div>
      </div>
      <div className="filters">
        <label>
          Type
          <select value={typeFilter} onChange={(event) => setTypeFilter(event.target.value as 'ALL' | 'PD' | 'MD')}>
            <option value="ALL">All</option>
            <option value="PD">PD</option>
            <option value="MD">MD</option>
          </select>
        </label>
        <label>
          Direction
          <select
            value={directionFilter}
            onChange={(event) => setDirectionFilter(event.target.value as 'ALL' | 'IN' | 'OUT')}
          >
            <option value="ALL">All</option>
            <option value="IN">Inbound</option>
            <option value="OUT">Outbound</option>
          </select>
        </label>
      </div>
      {error && <div className="error">{error}</div>}
      <div className="table-wrapper">
        <table>
          <thead>
            <tr>
              <th>Direction</th>
              <th>Type</th>
              <th>Msg ID</th>
              <th>Source IP</th>
              <th>Dest IP</th>
              <th>Payload</th>
              <th>Timestamp</th>
            </tr>
          </thead>
          <tbody>
            {logs.map((log) => (
              <tr key={log.id}>
                <td>{log.direction}</td>
                <td>{log.type}</td>
                <td>{log.msg_id}</td>
                <td>{log.src_ip || '—'}</td>
                <td>{log.dst_ip || '—'}</td>
                <td>
                  <code>{previewPayload(log.payload_hex)}</code>
                </td>
                <td>{log.timestamp_utc ? new Date(log.timestamp_utc).toLocaleString() : '—'}</td>
              </tr>
            ))}
            {logs.length === 0 && !loading && (
              <tr>
                <td colSpan={7} style={{ textAlign: 'center' }}>
                  No log entries found.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </section>
  );
}
