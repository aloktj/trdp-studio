import { useState } from 'react';
import { TrdpLogsPage } from './TrdpLogsPage';
import { AppLogsPage } from './AppLogsPage';

type Tab = 'trdp' | 'app';

export function LogsPage() {
  const [activeTab, setActiveTab] = useState<Tab>('trdp');

  return (
    <div>
      <h1>Logs</h1>
      <div className="tabs">
        <button type="button" className={activeTab === 'trdp' ? 'active' : ''} onClick={() => setActiveTab('trdp')}>
          TRDP Logs
        </button>
        <button type="button" className={activeTab === 'app' ? 'active' : ''} onClick={() => setActiveTab('app')}>
          Application Logs
        </button>
      </div>
      {activeTab === 'trdp' ? <TrdpLogsPage /> : <AppLogsPage />}
    </div>
  );
}
